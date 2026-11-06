package lemuri.be.monitoring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.ByteArrayOutputStream;
import java.io.DataOutputStream;
import java.io.IOException;
import java.math.BigInteger;
import java.nio.charset.StandardCharsets;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import lemuri.be.dto.Host;
import lemuri.be.dto.Security;
import lemuri.be.dto.Security.Level;
import lemuri.be.dto.Updates;

class SecurityAuditTest {
    private static final SecurityAudit.Connection MODERN = new SecurityAudit.Connection(
            "SSH-2.0-OpenSSH_9.6p1 Ubuntu-3ubuntu13.5", "mlkem768x25519-sha256", "chacha20-poly1305@openssh.com",
            "hmac-sha2-256-etm@openssh.com", true, "ssh-ed25519", 0);

    private static Level level(Security s, String id) {
        return s.checks().stream().filter(c -> c.id().equals(id)).findFirst().orElseThrow(() -> new AssertionError(id)).level();
    }

    private static boolean has(Security s, String id) {
        return s.checks().stream().anyMatch(c -> c.id().equals(id));
    }

    @Test
    void aModernHardenedServerIsAllGreen() {
        SecurityAudit.Server srv = SecurityAudit.parse(String.join("\n",
                "I 1000",
                "F /etc/ssh/sshd_config",
                "L Include /etc/ssh/sshd_config.d/*.conf",
                "L Port 2222",
                "L PasswordAuthentication no",
                "L KbdInteractiveAuthentication no",
                "L PermitRootLogin no",
                "L UsePAM yes",
                "G fail2ban", ""));
        Security s = SecurityAudit.audit(Host.AuthMethod.KEY, new SecurityAudit.ClientKey("ED25519", 256, true), MODERN, srv, null, 0)
                .withUpdates(new Updates(0, 0, false, null, null, 0));
        for (Security.Check c : s.checks()) assertEquals(Level.OK, c.level(), c.id());
        assertEquals(1.0, s.score());
        assertTrue(has(s, "server.updates"));
    }

    @Test
    void anOpenServerIsRed() {
        SecurityAudit.Connection old = new SecurityAudit.Connection("SSH-2.0-OpenSSH_7.2p2 Ubuntu-4ubuntu2.10",
                "diffie-hellman-group14-sha1", "aes128-cbc", "hmac-sha1-etm@openssh.com", false, "ssh-rsa", 1024);
        // Nothing written: the defaults of OpenSSH, passwords on.
        SecurityAudit.Server srv = SecurityAudit.parse("I 0\nF /etc/ssh/sshd_config\nL PermitRootLogin yes\n");
        Security s = SecurityAudit.audit(Host.AuthMethod.PASSWORD, null, old, srv, null, 0);
        for (String id : List.of("conn.version", "conn.kex", "conn.cipher", "conn.terrapin", "conn.hostkey", "client.auth",
                "server.password", "server.root", "server.bruteforce"))
            assertEquals(Level.BAD, level(s, id), id);
        assertEquals(Level.WARN, level(s, "conn.mac"));
        assertEquals(Level.WARN, level(s, "server.port"));
        assertEquals(Level.WARN, level(s, "server.user"));
        assertFalse(has(s, "client.key"), "no key file with a password");
        assertTrue(s.score() < 0.2, String.valueOf(s.score()));
    }

    @Test
    void theFirstValueWinsAndIncludesComeInTheirPlace() {
        Map<String, List<String>> files = new LinkedHashMap<>();
        // As on Ubuntu cloud images: the include on top, and cloud-init turning passwords on before the main file.
        files.put("/etc/ssh/sshd_config", List.of("Include /etc/ssh/sshd_config.d/*.conf", "PasswordAuthentication no",
                "Match User backup", "  PasswordAuthentication yes", "  PermitRootLogin yes"));
        files.put("/etc/ssh/sshd_config.d/60-later.conf", List.of("PermitRootLogin no"));
        files.put("/etc/ssh/sshd_config.d/50-cloud-init.conf", List.of("# comment", "PasswordAuthentication=yes"));
        Map<String, String> d = SecurityAudit.sshdConfig(files);
        assertEquals("yes", d.get("passwordauthentication"));
        assertEquals("no", d.get("permitrootlogin"), "the Match block does not count");
    }

    @Test
    void sshdDashTWinsOverTheFiles() {
        SecurityAudit.Server srv = SecurityAudit.parse("I 0\nS port 22\nS port 2222\nS passwordauthentication no\n"
                + "F /etc/ssh/sshd_config\nL PasswordAuthentication yes\n");
        assertEquals("no", srv.sshd().get("passwordauthentication"));
        assertEquals("22", srv.sshd().get("port"));
    }

    @Test
    void unreadableSettingsLeaveOnlyWhatIsKnown() {
        SecurityAudit.Server srv = SecurityAudit.parse("Welcome!\nI 1000\n");
        assertNull(srv.sshd());
        Security s = SecurityAudit.audit(Host.AuthMethod.AGENT, null, MODERN, srv, null, 0);
        assertFalse(has(s, "server.password"));
        assertEquals(Level.WARN, level(s, "server.bruteforce"), "without passwords known to be on, only a warning");
        assertEquals(Level.OK, level(s, "server.user"));
    }

    @Test
    void passwordsThroughPamAreAWarning() {
        SecurityAudit.Server srv = SecurityAudit.parse("I 1000\nF /etc/ssh/sshd_config\nL PasswordAuthentication no\nL UsePAM yes\n");
        Security s = SecurityAudit.audit(Host.AuthMethod.KEY, null, MODERN, srv, null, 0);
        assertEquals(Level.WARN, level(s, "server.password"));
        assertEquals("KbdInteractiveAuthentication no", s.checks().stream().filter(c -> c.id().equals("server.password")).findFirst().get().fix());
    }

    @Test
    void terrapinOnlyWhereItApplies() {
        SecurityAudit.Connection gcm = new SecurityAudit.Connection("SSH-2.0-OpenSSH_8.9p1", "curve25519-sha256",
                "aes256-gcm@openssh.com", "hmac-sha2-256", false, "rsa-sha2-512", 3072);
        Security s = SecurityAudit.audit(Host.AuthMethod.KEY, null, gcm, null, null, 0);
        assertEquals(Level.OK, level(s, "conn.terrapin"));
        assertEquals(Level.OK, level(s, "conn.hostkey"));
        SecurityAudit.Connection chacha = new SecurityAudit.Connection("SSH-2.0-OpenSSH_8.9p1", "curve25519-sha256",
                "chacha20-poly1305@openssh.com", "hmac-sha2-256", false, "ssh-ed25519", 0);
        assertEquals(Level.BAD, level(SecurityAudit.audit(Host.AuthMethod.KEY, null, chacha, null, null, 0), "conn.terrapin"));
    }

    @Test
    void clientKeys() {
        Security rsa = SecurityAudit.audit(Host.AuthMethod.KEY, new SecurityAudit.ClientKey("RSA", 2048, false), MODERN, null, null, 0);
        assertEquals(Level.WARN, level(rsa, "client.key"));
        assertEquals(Level.WARN, level(rsa, "client.passphrase"));
        Security dsa = SecurityAudit.audit(Host.AuthMethod.KEY, new SecurityAudit.ClientKey("DSA", 1024, true), MODERN, null, null, 0);
        assertEquals(Level.BAD, level(dsa, "client.key"));
    }

    @Test
    void securityUpdatesComeFromTheSummary() {
        Security s = SecurityAudit.audit(Host.AuthMethod.KEY, null, MODERN, null, null, 0);
        assertEquals(Level.BAD, level(s.withUpdates(new Updates(5, 2, false, null, null, 0)), "server.updates"));
        assertFalse(has(s.withUpdates(new Updates(5, null, false, null, null, 0)), "server.updates"), "unknown: not shown");
        assertEquals(1, s.withUpdates(new Updates(5, 2, false, null, null, 0)).withUpdates(new Updates(0, 0, false, null, null, 0))
                .checks().stream().filter(c -> c.id().equals("server.updates")).count());
    }

    @Test
    void rsaBitsFromTheKeyBlob() throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        DataOutputStream out = new DataOutputStream(b);
        byte[] type = "ssh-rsa".getBytes(StandardCharsets.US_ASCII);
        byte[] e = BigInteger.valueOf(65537).toByteArray();
        byte[] n = BigInteger.ONE.shiftLeft(3071).add(BigInteger.ONE).toByteArray();
        for (byte[] f : List.of(type, e, n)) {
            out.writeInt(f.length);
            out.write(f);
        }
        assertEquals(3072, SecurityAudit.rsaBits(b.toByteArray()));
        assertEquals(0, SecurityAudit.rsaBits(new byte[] {0, 0, 0, 99}));
    }

    /** The command, really run by a Linux shell as the server would: it ends and gives at least the user id. */
    @Test
    @EnabledOnOs(OS.LINUX)
    void commandRuns() throws Exception {
        Process p = new ProcessBuilder("sh", "-c", SecurityAudit.COMMAND).redirectErrorStream(true).start();
        assertTrue(p.waitFor(30, TimeUnit.SECONDS), "the command must finish");
        String output = new String(p.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        assertEquals(0, p.exitValue(), output);
        assertNotNull(SecurityAudit.parse(output).uid(), output);
    }
}
