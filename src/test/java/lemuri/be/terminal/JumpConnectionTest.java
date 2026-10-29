package lemuri.be.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.jcraft.jsch.ChannelExec;
import com.jcraft.jsch.JSchException;
import com.jcraft.jsch.Session;
import com.jcraft.jsch.UserInfo;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.List;
import org.apache.sshd.server.Environment;
import org.apache.sshd.server.ExitCallback;
import org.apache.sshd.server.SshServer;
import org.apache.sshd.server.channel.ChannelSession;
import org.apache.sshd.server.command.Command;
import org.apache.sshd.server.forward.AcceptAllForwardingFilter;
import org.apache.sshd.server.keyprovider.SimpleGeneratorHostKeyProvider;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;

/**
 * A real connection through a jump host: two SSH servers in memory (Apache MINA SSHD) on loopback, the
 * bastion allowing forwarding and the target answering a command. The home is the fake one of the pom.
 */
class JumpConnectionTest {
    private SshServer bastion, target;

    private static SshServer server(String name, boolean forwarding) throws Exception {
        SshServer s = SshServer.setUpDefaultServer();
        s.setHost("127.0.0.1");
        s.setPort(0);
        Path key = Path.of(System.getProperty("user.home"), "sshd-" + name + ".key");
        s.setKeyPairProvider(new SimpleGeneratorHostKeyProvider(key));
        s.setPasswordAuthenticator((user, password, session) -> ("secret-" + name).equals(password));
        if (forwarding) s.setForwardingFilter(AcceptAllForwardingFilter.INSTANCE);
        s.setCommandFactory((channel, command) -> new Answer(name + ": " + command));
        s.start();
        return s;
    }

    /** A command that prints one line and ends. */
    private static final class Answer implements Command {
        private final String text;
        private OutputStream out;
        private ExitCallback exit;

        Answer(String text) {
            this.text = text;
        }

        @Override public void setInputStream(InputStream in) {}
        @Override public void setOutputStream(OutputStream out) { this.out = out; }
        @Override public void setErrorStream(OutputStream err) {}
        @Override public void setExitCallback(ExitCallback callback) { exit = callback; }

        @Override
        public void start(ChannelSession channel, Environment env) throws IOException {
            out.write((text + "\n").getBytes(StandardCharsets.UTF_8));
            out.flush();
            exit.onExit(0);
        }

        @Override public void destroy(ChannelSession channel) {}
    }

    /** Accepts new host keys and answers with the password of whichever server asks. */
    private static final class Answers implements UserInfo {
        private final String password;

        Answers(Host h) {
            password = h.note;
        }

        @Override public String getPassphrase() { return null; }
        @Override public String getPassword() { return password; }
        @Override public boolean promptPassword(String message) { return true; }
        @Override public boolean promptPassphrase(String message) { return false; }
        @Override public boolean promptYesNo(String message) { return true; }
        @Override public void showMessage(String message) {}
    }

    private static Host host(String name, int port, String password) {
        Host h = new Host();
        h.id = name;
        h.name = name;
        h.address = "127.0.0.1";
        h.port = port;
        h.user = "ops";
        h.authMethod = Host.AuthMethod.PASSWORD;
        h.note = password; // where the test keeps each server's password
        return h;
    }

    @BeforeEach
    void start() throws Exception {
        assertTrue(System.getProperty("user.home").contains("target"), "the tests must not use the real home");
        Files.createDirectories(Path.of(System.getProperty("user.home"), ".ssh"));
        bastion = server("bastion", true);
        target = server("target", false);
    }

    @AfterEach
    void stop() throws Exception {
        bastion.stop(true);
        target.stop(true);
    }

    private static String run(Session s, String command) throws Exception {
        ChannelExec c = (ChannelExec) s.openChannel("exec");
        c.setCommand(command);
        var in = c.getInputStream();
        c.connect(5_000);
        String out = SshTransport.readAtMost(in, 4096).strip();
        c.disconnect();
        return out;
    }

    @Test
    void theTargetIsReachedThroughTheBastionAndClosingClosesBoth() throws Exception {
        Host t = host("target", target.getPort(), "secret-target");
        t.jump = "bastion";
        Host resolved = Jumps.resolve(t, List.of(host("bastion", bastion.getPort(), "secret-bastion"), t));
        assertEquals(1, resolved.route.size());

        Session s = SshTransport.open(resolved, Answers::new, m -> {}, 10_000);
        assertEquals("target: uptime", run(s, "uptime"));
        assertEquals(1, bastion.getActiveSessions().size());

        s.disconnect();
        for (int i = 0; i < 50 && !bastion.getActiveSessions().isEmpty(); i++) Thread.sleep(100);
        assertTrue(bastion.getActiveSessions().isEmpty(), "the jump session must close with the target one");
    }

    @Test
    void aFailingHopIsNamedInTheError() {
        Host t = host("target", target.getPort(), "secret-target");
        t.jump = "bastion";
        Host resolved = Jumps.resolve(t, List.of(host("bastion", bastion.getPort(), "wrong"), t));
        JSchException e = assertThrows(JSchException.class, () -> SshTransport.open(resolved, Answers::new, m -> {}, 10_000));
        assertTrue(e.getMessage().contains("ops@127.0.0.1:" + bastion.getPort()), e.getMessage());
        assertTrue(bastion.getActiveSessions().isEmpty() || waitEmpty(bastion));
    }

    @Test
    void aHostWithAnInvalidJumpDoesNotConnect() {
        Host t = host("target", target.getPort(), "secret-target");
        t.jump = "bastion,,";
        Host resolved = Jumps.resolve(t, List.of(t));
        assertThrows(JSchException.class, () -> SshTransport.open(resolved, Answers::new, m -> {}, 10_000));
        assertTrue(target.getActiveSessions().isEmpty());
    }

    private static boolean waitEmpty(SshServer s) {
        for (int i = 0; i < 50 && !s.getActiveSessions().isEmpty(); i++) {
            try {
                Thread.sleep(100);
            } catch (InterruptedException e) {
                return false;
            }
        }
        return s.getActiveSessions().isEmpty();
    }
}
