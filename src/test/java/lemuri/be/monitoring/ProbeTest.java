package lemuri.be.monitoring;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.io.BufferedReader;
import java.io.InputStreamReader;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;

/** The probe greets sshd before closing: a silent connection is what fail2ban counts as an attempt. */
class ProbeTest {
    @Test
    void answersTheServerVersionLine() throws Exception {
        try (ServerSocket server = new ServerSocket(0, 1, InetAddress.getLoopbackAddress())) {
            CompletableFuture<String> received = CompletableFuture.supplyAsync(() -> {
                try (Socket c = server.accept()) {
                    c.getOutputStream().write("SSH-2.0-OpenSSH_9.6\r\n".getBytes(StandardCharsets.US_ASCII));
                    return new BufferedReader(new InputStreamReader(c.getInputStream(), StandardCharsets.US_ASCII)).readLine();
                } catch (Exception e) {
                    return "error: " + e;
                }
            });
            try (Socket s = new Socket(InetAddress.getLoopbackAddress(), server.getLocalPort())) {
                Probe.greetAndClose(s);
            }
            assertEquals("SSH-2.0-Lemuri_probe", received.get(5, TimeUnit.SECONDS));
        }
    }

    /** Set to "never", the probe connects to nobody on its own, not even to a new host: only when asked. */
    @Test
    void neverMeansOnlyWhenAsked() throws Exception {
        try (ServerSocket server = new ServerSocket(0, 5, InetAddress.getLoopbackAddress())) {
            AtomicInteger connections = new AtomicInteger();
            Thread.ofVirtual().start(() -> {
                while (!server.isClosed()) {
                    try (Socket c = server.accept()) {
                        connections.incrementAndGet();
                        c.getOutputStream().write("SSH-2.0-OpenSSH_9.6\r\n".getBytes(StandardCharsets.US_ASCII));
                    } catch (Exception e) {
                        // closed at the end
                    }
                }
            });
            Probe p = new Probe();
            CompletableFuture<Map<String, HostStatus>> outcome = new CompletableFuture<>();
            try {
                p.every(0);
                p.launch(outcome::complete);
                Host h = new Host();
                h.id = "h1";
                h.address = "127.0.0.1";
                h.port = server.getLocalPort();
                p.list(List.of(h));
                Thread.sleep(500);
                assertEquals(0, connections.get());
                p.all();
                assertEquals(HostStatus.ONLINE, outcome.get(5, TimeUnit.SECONDS).get("h1"));
                assertEquals(1, connections.get());
            } finally {
                p.stop();
            }
        }
    }
}
