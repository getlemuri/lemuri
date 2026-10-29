package lemuri.distros;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.jcraft.jsch.ChannelSftp;
import java.io.File;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Base64;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.stream.Stream;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.DynamicTest;
import org.junit.jupiter.api.TestFactory;
import org.junit.jupiter.api.condition.EnabledIfSystemProperty;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.dto.MetricsResult;
import lemuri.be.dto.SessionStatus;
import lemuri.be.dto.Updates;
import lemuri.be.monitoring.Metrics;
import lemuri.be.monitoring.Probe;
import lemuri.be.service.Vault;
import lemuri.be.terminal.Jumps;
import lemuri.be.terminal.SftpFiles;
import lemuri.be.terminal.SshCli;
import lemuri.be.terminal.SshTransport;

/**
 * Lemuri against real SSH servers of other distributions, in containers (src/test/distros/run.sh starts them
 * and passes -Dlemuri.distros=debian=2201,ubuntu=2202,…). For each one: reachability, sign-in with the saved
 * password, an interactive shell, metrics, updates and reboot, an SFTP upload, sign-in with a key, a jump
 * host, and "lemuri -s" with the real ssh and the askpass of bin/lemuri. Skipped without the property.
 */
@EnabledIfSystemProperty(named = "lemuri.distros", matches = ".+")
class DistrosTest {
    static final String USER = "deploy", PASSWORD = "deploy-pass";
    /** Systems that can tell whether they need a reboot (the others answer "cannot tell"). */
    static final List<String> REBOOT_KNOWN = List.of("debian", "ubuntu", "fedora", "rocky", "opensuse");

    static Map<String, Integer> distros() {
        Map<String, Integer> m = new LinkedHashMap<>();
        for (String p : System.getProperty("lemuri.distros").split(",")) m.put(p.split("=")[0], Integer.parseInt(p.split("=")[1]));
        return m;
    }

    @BeforeAll
    static void freshKnownHosts() throws Exception {
        Path kh = Path.of(System.getProperty("user.home"), ".ssh", "known_hosts");
        assertTrue(kh.toString().contains("target"), "never the real ~/.ssh: " + kh);
        Files.createDirectories(kh.getParent());
        Files.deleteIfExists(kh);
    }

    static Host host(String name, int port) {
        Host h = new Host();
        h.id = name;
        h.name = name;
        h.address = "127.0.0.1";
        h.port = port;
        h.user = USER;
        h.authMethod = Host.AuthMethod.PASSWORD;
        h.state = HostStatus.ONLINE;
        return h;
    }

    /** A vault with the passwords of the servers. */
    static final class Passwords implements Vault {
        final Map<String, String> secrets = new HashMap<>();
        @Override public boolean exists() { return true; }
        @Override public void onUnlock(Runnable r) {}
        @Override public void onLock(Runnable r) {}
        @Override public boolean isOpen() { return true; }
        @Override public void create(char[] main) {}
        @Override public boolean open(char[] main) { return true; }
        @Override public void lock() {}
        @Override public boolean changePassword(char[] current, char[] fresh) { return true; }
        @Override public String password(String key) { return secrets.get(key); }
        @Override public void remember(String key, String value) { secrets.put(key, value); }
        @Override public void forget(String key) { secrets.remove(key); }
        @Override public Map<String, String> entries() { return secrets; }
        @Override public void replaceAll(Map<String, String> entries) {}
    }

    /** The user at the keyboard: accepts new host keys, types the password. */
    static SshTransport.UserPrompts user() {
        return new SshTransport.UserPrompts() {
            @Override public boolean confirm(String message) { return true; }
            @Override public String password(String message) { return PASSWORD; }
            @Override public String passphrase(String message) { return null; }
            @Override public String reply(String question) { return null; }
        };
    }

    @TestFactory
    Stream<DynamicTest> everyDistro() {
        List<DynamicTest> tests = new ArrayList<>();
        Map<String, Integer> all = distros();
        for (var e : all.entrySet()) {
            String name = e.getKey();
            int port = e.getValue();
            tests.add(DynamicTest.dynamicTest(name + ": reachable", () -> reachable(name, port)));
            tests.add(DynamicTest.dynamicTest(name + ": interactive shell with password", () -> shell(host(name, port))));
            tests.add(DynamicTest.dynamicTest(name + ": metrics, updates and reboot", () -> metrics(name, host(name, port))));
            tests.add(DynamicTest.dynamicTest(name + ": SFTP upload", () -> upload(host(name, port))));
            tests.add(DynamicTest.dynamicTest(name + ": sign-in with a key", () -> key(name, port)));
            tests.add(DynamicTest.dynamicTest(name + ": lemuri -s with the vault password", () -> cli(host(name, port), List.of())));
        }
        // Jump hosts: every server reached through the first one, by its name on the containers' network.
        String bastion = all.keySet().iterator().next();
        for (String name : all.keySet()) {
            if (name.equals(bastion)) continue;
            tests.add(DynamicTest.dynamicTest(name + ": through " + bastion + " (jump host)", () -> jump(bastion, all.get(bastion), name)));
            tests.add(DynamicTest.dynamicTest(name + ": lemuri -s through " + bastion, () -> {
                Host b = host(bastion, all.get(bastion));
                Host t = behind(bastion, name);
                cli(t, List.of(b));
            }));
        }
        return tests.stream();
    }

    void reachable(String name, int port) throws Exception {
        Probe p = new Probe();
        CompletableFuture<Map<String, HostStatus>> got = new CompletableFuture<>();
        p.launch(got::complete);
        p.list(List.of(host(name, port)));
        p.all();
        assertEquals(HostStatus.ONLINE, got.get(20, TimeUnit.SECONDS).get(name));
        p.stop();
    }

    void shell(Host h) throws Exception {
        // A host behind a jump host comes with its route already resolved: resolved again here it would be lost.
        SshTransport t = new SshTransport(h.route != null ? h : Jumps.resolve(h, List.of()), x -> user());
        StringBuilder out = new StringBuilder();
        CompletableFuture<SessionStatus> connected = new CompletableFuture<>();
        t.onData(d -> { synchronized (out) { out.append(d); } });
        t.onState((s, m) -> { if (s == SessionStatus.CONNECTED || s == SessionStatus.ERROR) connected.complete(s); });
        t.launch(100, 30);
        assertEquals(SessionStatus.CONNECTED, connected.get(30, TimeUnit.SECONDS), () -> out.toString());
        t.send("echo LEMURI-$((40+2)); printf '%s\\n' \"$SHELL\"\n");
        long end = System.currentTimeMillis() + 15_000;
        while (System.currentTimeMillis() < end) {
            synchronized (out) { if (out.toString().contains("LEMURI-42")) break; }
            Thread.sleep(100);
        }
        t.close();
        synchronized (out) { assertTrue(out.toString().contains("LEMURI-42"), out.toString()); }
    }

    void metrics(String name, Host h) throws Exception {
        Passwords v = new Passwords();
        v.secrets.put(h.id, PASSWORD);
        Metrics m = new Metrics(v);
        try {
            CompletableFuture<MetricsResult> r = new CompletableFuture<>();
            m.refresh(h, true, r::complete);
            MetricsResult got = r.get(60, TimeUnit.SECONDS);
            assertNull(got.error(), "metrics: " + got.error());
            assertTrue(got.resources().cpu() > 0);
            assertNotNull(got.resources().cpuUsage());
            assertTrue(got.resources().memTotalKb() > 0 && got.resources().memFreeKb() > 0);
            assertTrue(got.resources().root() != null || !got.resources().disks().isEmpty(), "disks: " + got.resources().disks());
            assertTrue(got.resources().onForS() > 0);
            CompletableFuture<Updates> u = new CompletableFuture<>();
            m.updates(h, true, u::complete);
            Updates up = u.get(90, TimeUnit.SECONDS);
            System.out.println("DISTRO " + name + " updates=" + up.packages() + " security=" + up.security() + " reboot=" + up.reboot() + " because=" + up.rebootBecause());
            assertNull(up.error(), "updates: " + up.error());
            assertNotNull(up.packages(), "the package manager answered");
            if (up.security() != null) assertTrue(up.security() <= up.packages(), "security updates are some of the updates");
            if (REBOOT_KNOWN.contains(name)) assertNotNull(up.reboot(), "the system says whether it needs a reboot");
            if (name.equals("debian")) assertEquals(Boolean.TRUE, up.reboot(), "/var/run/reboot-required was created by run.sh");
        } finally {
            m.stop();
        }
    }

    void upload(Host h) throws Exception {
        SshTransport t = new SshTransport(Jumps.resolve(h, List.of()), x -> user());
        CompletableFuture<SessionStatus> connected = new CompletableFuture<>();
        t.onData(d -> {});
        t.onState((s, msg) -> { if (s == SessionStatus.CONNECTED || s == SessionStatus.ERROR) connected.complete(s); });
        t.launch(80, 24);
        assertEquals(SessionStatus.CONNECTED, connected.get(30, TimeUnit.SECONDS));
        File f = File.createTempFile("lemuri upload ", ".txt");
        Files.writeString(f.toPath(), "hello from lemuri\n");
        ChannelSftp c = t.openSftp();
        try {
            SftpFiles.upload(c, f, "/home/" + USER, new SftpFiles.Watch() {
                @Override public boolean cancelled() { return false; }
                @Override public void skipped() {}
                @Override public void file(String name) {}
                @Override public void init(int op, String src, String dest, long max) {}
                @Override public boolean count(long n) { return true; }
                @Override public void end() {}
            });
            assertEquals(18, c.stat("/home/" + USER + "/" + f.getName()).getSize());
            c.rm("/home/" + USER + "/" + f.getName());
        } finally {
            c.disconnect();
            t.close();
            f.delete();
        }
    }

    void key(String name, int port) throws Exception {
        Host h = host(name + "-key", port);
        h.authMethod = Host.AuthMethod.KEY;
        h.key = System.getProperty("lemuri.distros.key");
        Metrics m = new Metrics(new Passwords());
        try {
            CompletableFuture<MetricsResult> r = new CompletableFuture<>();
            m.refresh(h, true, r::complete);
            MetricsResult got = r.get(60, TimeUnit.SECONDS);
            assertNull(got.error(), "with the key: " + got.error());
        } finally {
            m.stop();
        }
    }

    /** {@code name} as seen from the bastion: its container name on their network, port 22. */
    static Host behind(String bastion, String name) {
        Host t = host(name + "-behind", 22);
        t.address = "lemuri-test-" + name;
        t.jump = bastion;
        return t;
    }

    void jump(String bastion, int bastionPort, String name) throws Exception {
        Host b = host(bastion, bastionPort);
        Host t = behind(bastion, name);
        shell(Jumps.resolve(t, List.of(b, t)));
        // Metrics through the jump host too, each hop with its saved password.
        Passwords v = new Passwords();
        v.secrets.put(b.id, PASSWORD);
        v.secrets.put(t.id, PASSWORD);
        Metrics m = new Metrics(v);
        try {
            CompletableFuture<MetricsResult> r = new CompletableFuture<>();
            m.refresh(Jumps.resolve(t, List.of(b, t)), true, r::complete);
            assertNull(r.get(60, TimeUnit.SECONDS).error());
        } finally {
            m.stop();
        }
    }

    /** What "lemuri -s" runs: ssh with the configuration of SshCli, the saved passwords answered by bin/lemuri. */
    void cli(Host target, List<Host> others) throws Exception {
        List<Host> known = new ArrayList<>(others);
        known.add(target);
        Host h = Jumps.resolve(target, known);
        Path dir = Files.createTempDirectory("lemuri-cli");
        Path conf = dir.resolve("h.conf");
        Files.writeString(conf, SshCli.config(h));
        StringBuilder table = new StringBuilder();
        for (Host x : known) table.append(x.user).append('@').append(x.address).append('\t')
                .append(Base64.getEncoder().encodeToString(PASSWORD.getBytes(StandardCharsets.UTF_8))).append('\n');
        List<String> command = new ArrayList<>(List.of("ssh", "-o", "UserKnownHostsFile=" + dir.resolve("kh"), "-o", "StrictHostKeyChecking=accept-new",
                "-o", "BatchMode=no", "-o", "PubkeyAuthentication=no"));
        command.addAll(SshCli.command(conf.toString(), List.of("echo", "CLI-$((6*7))")).subList(1, 4));
        command.add("echo CLI-$((6*7))");
        // The jump host's ssh does not get the options of the command line: the same ones go into the file.
        Files.writeString(conf, "UserKnownHostsFile " + dir.resolve("kh") + "\nStrictHostKeyChecking accept-new\nPubkeyAuthentication no\n" + SshCli.config(h));
        ProcessBuilder pb = new ProcessBuilder(command).redirectErrorStream(true).redirectInput(ProcessBuilder.Redirect.from(new File("/dev/null")));
        pb.environment().put("LEMURI_PASSWORDS", Base64.getEncoder().encodeToString(table.toString().getBytes(StandardCharsets.UTF_8)));
        pb.environment().put("LEMURI_ASKPASS", "1");
        pb.environment().put("SSH_ASKPASS", Path.of("bin", "lemuri").toAbsolutePath().toString());
        pb.environment().put("SSH_ASKPASS_REQUIRE", "force");
        pb.environment().put("XDG_RUNTIME_DIR", dir.toString());
        Process p = pb.start();
        assertTrue(p.waitFor(60, TimeUnit.SECONDS), "ssh finished");
        String out = new String(p.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        assertTrue(out.contains("CLI-42"), "ssh output: " + out);
    }
}
