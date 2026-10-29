package lemuri.be.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertThrows;

import com.jcraft.jsch.JSch;
import com.jcraft.jsch.JSchException;
import com.jcraft.jsch.Session;
import java.io.IOException;
import java.net.ConnectException;
import java.net.ServerSocket;
import java.net.UnknownHostException;
import org.junit.jupiter.api.Test;

/** The errors of JSch and of the network become sentences for the user. */
class SshErrorsTest {
    /** What JSch throws when it tries {@code host:port}. */
    private static JSchException connect(String host, int port, int timeout) throws JSchException {
        Session s = new JSch().getSession("nobody", host, port);
        s.setConfig("StrictHostKeyChecking", "no");
        return assertThrows(JSchException.class, () -> s.connect(timeout));
    }

    @Test
    void aClosedPortIsRefused() throws Exception {
        int port;
        try (ServerSocket free = new ServerSocket(0)) {
            port = free.getLocalPort();
        }
        assertEquals("ssh.err.refused", SshErrors.key(connect("127.0.0.1", port, 3000)));
    }

    @Test
    void aServerThatNeverSpeaksTimesOut() throws Exception {
        // Accepts the connection but never sends its SSH version line.
        try (ServerSocket silent = new ServerSocket(0)) {
            assertEquals("ssh.err.timeout", SshErrors.key(connect("127.0.0.1", silent.getLocalPort(), 500)));
        }
    }

    @Test
    void aServerThatHangsUpIsExplained() throws Exception {
        try (ServerSocket rude = new ServerSocket(0)) {
            Thread t = new Thread(() -> {
                try {
                    rude.accept().close();
                } catch (IOException ignored) {
                    // the test ends anyway
                }
            });
            t.start();
            assertEquals("ssh.err.closedByServer", SshErrors.key(connect("127.0.0.1", rude.getLocalPort(), 3000)));
            t.join();
        }
    }

    @Test
    void theMessagesOfJschAreRecognised() {
        assertEquals("ssh.err.timeout", SshErrors.key(new JSchException("timeout: socket is not established")));
        assertEquals("ssh.err.auth", SshErrors.key(new JSchException("Auth fail for methods 'publickey,password'")));
        assertEquals("ssh.err.hostKey", SshErrors.key(new JSchException("reject HostKey: 203.0.113.10")));
        assertEquals("ssh.err.algorithms", SshErrors.key(new JSchException("Algorithm negotiation fail: algorithmName=\"kex\"")));
        assertEquals("ssh.err.key", SshErrors.key(new JSchException("invalid privatekey: id_rsa")));
        assertEquals("ssh.err.dropped", SshErrors.key(new JSchException("session is down")));
        assertEquals("ssh.err.unknownHost", SshErrors.key(new JSchException("java.net.UnknownHostException: web-03", new UnknownHostException("web-03"))));
        assertEquals("ssh.err.timeout", SshErrors.key(new JSchException("x", new ConnectException("Connection timed out"))));
        assertNull(SshErrors.key(new JSchException("something new")));
        assertEquals("something new", SshErrors.explain(new JSchException("something new")));
    }

    @Test
    void aJumpHostKeepsItsOwnSentence() {
        assertEquals("Jump host bastion: no", SshErrors.explain(new SshErrors.Explained("Jump host bastion: no", new JSchException("Auth fail"))));
    }
}
