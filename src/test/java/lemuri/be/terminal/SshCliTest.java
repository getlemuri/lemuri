package lemuri.be.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import org.junit.jupiter.api.io.TempDir;
import lemuri.be.dto.Host;

/** The ssh configuration of "lemuri -s": what it says, what it refuses, and that OpenSSH reads it as meant. */
class SshCliTest {
    private static Host host(String name, String address, int port, String user, String key, String jump) {
        Host h = new Host();
        h.id = name;
        h.name = name;
        h.address = address;
        h.port = port;
        h.user = user;
        h.key = key;
        h.authMethod = key != null ? Host.AuthMethod.KEY : Host.AuthMethod.AGENT;
        h.jump = jump;
        return h;
    }

    private final Host bastion = host("bastion", "198.51.100.7", 2222, "ops", "/home/me/.ssh/bastion 100%", null);
    private final Host db = host("db-01", "10.0.0.5", 22, "deploy", null, "bastion");

    @Test
    void theHostGoesThroughItsJumpHostWithItsOwnKey() {
        String c = SshCli.config(Jumps.resolve(db, List.of(bastion, db)));
        assertTrue(c.contains("Host lemuri-hop-1\n    HostName 198.51.100.7\n    Port 2222\n    User ops\n"
                + "    IdentityFile \"/home/me/.ssh/bastion 100%%\"\n    IdentitiesOnly yes\n"), c);
        assertTrue(c.contains("Host lemuri-target\n    HostName 10.0.0.5\n    Port 22\n    User deploy\n    ProxyJump lemuri-hop-1\n"), c);
        assertTrue(c.endsWith("Match all\nInclude ~/.ssh/config\n"), c);
        assertEquals(List.of("ssh", "-F", "/x.conf", "lemuri-target", "uptime"), SshCli.command("/x.conf", List.of("uptime")));
    }

    @Test
    void valuesThatWouldAddLinesOrOptionsAreRefused() {
        assertThrows(IllegalArgumentException.class, () -> SshCli.config(host("a", "x\n    ProxyCommand touch /tmp/p", 22, "u", null, null)));
        assertThrows(IllegalArgumentException.class, () -> SshCli.config(host("a", "-oProxyCommand=x", 22, "u", null, null)));
        assertThrows(IllegalArgumentException.class, () -> SshCli.config(host("a", "h", 22, "u x", null, null)));
        assertThrows(IllegalArgumentException.class, () -> SshCli.config(host("a", "h", 22, "u", "k\"\n    ProxyCommand x", null)));
        assertThrows(IllegalArgumentException.class, () -> SshCli.config(host("a", "h", 0, "u", null, null)));
        // A broken chain of jump hosts (a loop) is said, not written.
        Host a = host("a", "h", 22, "u", null, "b"), b = host("b", "h", 22, "u", null, "a");
        assertThrows(IllegalArgumentException.class, () -> SshCli.config(Jumps.resolve(a, List.of(a, b))));
    }

    @Test
    @EnabledOnOs(OS.LINUX)
    void openSshReadsItAsMeant(@TempDir Path dir) throws Exception {
        if (!Files.isExecutable(Path.of("/usr/bin/ssh"))) return;
        Path file = dir.resolve("db.conf");
        Files.writeString(file, SshCli.config(Jumps.resolve(db, List.of(bastion, db))));
        // ssh -G prints the configuration it would use, without connecting.
        Process p = new ProcessBuilder("ssh", "-G", "-F", file.toString(), SshCli.TARGET).redirectErrorStream(true).start();
        assertTrue(p.waitFor(10, TimeUnit.SECONDS));
        String out = new String(p.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        assertTrue(out.contains("hostname 10.0.0.5\n"), out);
        assertTrue(out.contains("user deploy\n"), out);
        assertTrue(out.contains("proxyjump lemuri-hop-1\n"), out);
        p = new ProcessBuilder("ssh", "-G", "-F", file.toString(), "lemuri-hop-1").redirectErrorStream(true).start();
        assertTrue(p.waitFor(10, TimeUnit.SECONDS));
        out = new String(p.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        assertTrue(out.contains("port 2222\n"), out);
        // -G prints the value before its tokens are expanded: %% is still doubled.
        assertTrue(out.contains("identityfile /home/me/.ssh/bastion 100%%\n"), out);
    }
}
