package lemuri.be.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.InputStream;
import java.io.OutputStream;
import java.nio.file.Path;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.TimeUnit;
import org.apache.sshd.server.Environment;
import org.apache.sshd.server.ExitCallback;
import org.apache.sshd.server.Signal;
import org.apache.sshd.server.SshServer;
import org.apache.sshd.server.channel.ChannelSession;
import org.apache.sshd.server.command.Command;
import org.apache.sshd.server.keyprovider.SimpleGeneratorHostKeyProvider;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;

/**
 * The terminal is resized while the connection is still being made (its first size can come before it is laid
 * out, one or two columns wide): the shell must end up with the last size, or bash draws the prompt and the
 * typed text for a terminal that narrow, and nothing shows. And a terminal closed while it connects must not
 * leave the connection open behind it. On a slow SSH server in memory.
 */
class TerminalSizeTest {
    private SshServer server;
    private volatile String size = "";
    private final CountDownLatch shell = new CountDownLatch(1);

    @AfterEach
    void stop() throws Exception {
        if (server != null) server.stop();
    }

    /** A shell that only keeps the terminal size it is given, at the start and at every resize. */
    private final class Shell implements Command {
        @Override public void setInputStream(InputStream in) {}
        @Override public void setOutputStream(OutputStream out) {}
        @Override public void setErrorStream(OutputStream err) {}
        @Override public void setExitCallback(ExitCallback callback) {}

        @Override
        public void start(ChannelSession channel, Environment env) {
            size = env.getEnv().get(Environment.ENV_COLUMNS) + "x" + env.getEnv().get(Environment.ENV_LINES);
            env.addSignalListener((c, s) -> size = env.getEnv().get(Environment.ENV_COLUMNS) + "x" + env.getEnv().get(Environment.ENV_LINES), Signal.WINCH);
            shell.countDown();
        }

        @Override public void destroy(ChannelSession channel) {}
    }

    /** A far server: the sign-in takes a while, and meanwhile the terminal gets its real size (or is closed). */
    private SshTransport slowServer() throws Exception {
        server = SshServer.setUpDefaultServer();
        server.setHost("127.0.0.1");
        server.setPort(0);
        server.setKeyPairProvider(new SimpleGeneratorHostKeyProvider(Path.of(System.getProperty("user.home"), "sshd-size.key")));
        server.setPasswordAuthenticator((user, password, session) -> {
            try {
                Thread.sleep(800);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
            return "x".equals(password);
        });
        server.setShellFactory(channel -> new Shell());
        server.start();

        Host h = new Host();
        h.id = "size";
        h.name = "size";
        h.address = "127.0.0.1";
        h.port = server.getPort();
        h.user = "u";
        h.authMethod = Host.AuthMethod.PASSWORD;
        return new SshTransport(h, x -> new SshTransport.UserPrompts() {
            @Override public boolean confirm(String message) { return true; }
            @Override public String password(String message) { return "x"; }
            @Override public String passphrase(String message) { return null; }
            @Override public String reply(String question) { return null; }
        });
    }

    @Test
    void theShellGetsTheLastSizeEvenIfItChangedWhileConnecting() throws Exception {
        SshTransport t = slowServer();
        try {
            t.launch(2, 1);
            t.resize(180, 50);
            assertTrue(shell.await(10, TimeUnit.SECONDS), "the shell starts");
            for (int i = 0; i < 40 && !size.equals("180x50"); i++) Thread.sleep(50);
            assertEquals("180x50", size);
        } finally {
            t.close();
        }
    }

    @Test
    void closingWhileSigningInLeavesNoConnection() throws Exception {
        SshTransport t = slowServer();
        t.launch(80, 24);
        t.close();
        // The sign-in goes on on the server for a while, then the connection opened for nothing must go away.
        for (int i = 0; i < 100 && (i < 30 || !server.getActiveSessions().isEmpty()); i++) Thread.sleep(50);
        assertTrue(server.getActiveSessions().isEmpty(), "no connection left open");
        assertEquals(1, shell.getCount(), "no shell started");
    }
}
