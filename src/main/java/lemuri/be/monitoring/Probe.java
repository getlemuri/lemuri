package lemuri.be.monitoring;

import java.io.IOException;
import java.io.InputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.service.ProbeService;

/**
 * Periodically checks whether the hosts answer on the SSH port: it is enough for
 * the TCP connection to open, no authentication needed. It runs every few minutes (as the
 * user chose, or never) and right away for new hosts or hosts with a changed address.
 * Every check leaves a line in the server's sshd log.
 *
 * <p>Before closing it exchanges the version line with sshd ({@link #greetAndClose}): a connection
 * closed without it is what fail2ban, in the "ddos" and "aggressive" modes of the sshd filter, counts as
 * an attempt ("kex_exchange_identification", "did not receive identification string").
 */
public final class Probe implements ProbeService {
    private static final int WAIT_MS = 3_000;

    private record Target(String id, String address, int port) {}

    private final ScheduledExecutorService clock = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread t = new Thread(r, "probe");
        t.setDaemon(true);
        return t;
    });
    private final ExecutorService tests = Executors.newVirtualThreadPerTaskExecutor();
    /** hostId -> "address:port" already tried, to recognise new or moved hosts. */
    private final Map<String, String> tried = new HashMap<>();
    private volatile List<Target> targets = List.of();
    private volatile Consumer<Map<String, HostStatus>> outcomes = m -> {};
    private volatile int minutes = DEFAULT_MINUTES;
    private boolean launched;
    private ScheduledFuture<?> round;

    @Override
    public synchronized void launch(Consumer<Map<String, HostStatus>> outcomes) {
        this.outcomes = outcomes;
        launched = true;
        schedule();
        if (minutes > 0) all();
    }

    @Override
    public synchronized void every(int minutes) {
        this.minutes = Math.max(0, minutes);
        if (launched) schedule();
    }

    private void schedule() {
        if (round != null) round.cancel(false);
        round = minutes > 0 && !clock.isShutdown() ? clock.scheduleWithFixedDelay(this::all, minutes, minutes, TimeUnit.MINUTES) : null;
    }

    @Override
    public void stop() {
        clock.shutdownNow();
        tests.shutdownNow();
    }

    @Override
    public void all() {
        test(targets);
    }

    @Override
    public synchronized void list(List<Host> hosts) {
        // Behind a jump host only the first hop can be reached from here: if it answers, the way in is open.
        targets = hosts.stream().map(h -> h.route != null && !h.route.isEmpty()
                ? new Target(h.id, h.route.get(0).address, h.route.get(0).port)
                : new Target(h.id, h.address, h.port)).toList();
        // Never: only when asked (all()).
        if (minutes > 0) test(targets.stream().filter(b -> !(b.address() + ":" + b.port()).equals(tried.get(b.id()))).toList());
    }

    private synchronized void test(List<Target> targets) {
        if (targets.isEmpty() || tests.isShutdown()) return;
        Map<String, CompletableFuture<HostStatus>> future = new HashMap<>();
        for (Target b : targets) {
            tried.put(b.id(), b.address() + ":" + b.port());
            future.put(b.id(), CompletableFuture.supplyAsync(() -> reachable(b) ? HostStatus.ONLINE : HostStatus.OFFLINE, tests));
        }
        CompletableFuture.allOf(future.values().toArray(CompletableFuture[]::new)).thenRun(() -> {
            Map<String, HostStatus> statuses = new HashMap<>();
            future.forEach((id, f) -> statuses.put(id, f.join()));
            outcomes.accept(statuses);
        });
    }

    private static boolean reachable(Target b) {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(b.address(), b.port()), WAIT_MS);
            greetAndClose(s);
            return true;
        } catch (IOException | IllegalArgumentException e) {
            return false;
        }
    }

    /** Reached: read the server version line and answer with ours. Whatever goes wrong here does not matter. */
    static void greetAndClose(Socket s) {
        try {
            s.setSoTimeout(WAIT_MS);
            InputStream in = s.getInputStream();
            StringBuilder line = new StringBuilder();
            // At most 255 bytes (RFC 4253): a line that does not arrive or does not end does not block the probe.
            for (int c; line.length() < 255 && (c = in.read()) != -1 && c != '\n'; ) line.append((char) c);
            if (line.toString().startsWith("SSH-")) {
                s.getOutputStream().write("SSH-2.0-Lemuri_probe\r\n".getBytes(StandardCharsets.US_ASCII));
                s.getOutputStream().flush();
            }
        } catch (IOException e) {
            // port open but not SSH, or server slow to greet: the host answers anyway
        }
    }
}
