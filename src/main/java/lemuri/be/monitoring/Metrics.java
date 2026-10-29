package lemuri.be.monitoring;

import com.jcraft.jsch.ChannelExec;
import com.jcraft.jsch.JSchException;
import com.jcraft.jsch.Session;
import com.jcraft.jsch.UIKeyboardInteractive;
import com.jcraft.jsch.UserInfo;
import java.io.IOException;
import java.io.InputStream;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.Deque;
import java.util.List;
import java.util.Map;
import java.util.Objects;
import java.util.Set;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
import lemuri.be.dto.Disk;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.dto.MetricsResult;
import lemuri.be.dto.Resources;
import lemuri.be.dto.Updates;
import lemuri.be.service.MetricsService;
import lemuri.be.service.Vault;
import lemuri.be.terminal.SshErrors;
import lemuri.be.terminal.SshTransport;
import lemuri.be.util.I18n;

/**
 * Real VPS resources, read over SSH from /proc and df. Every host has its own
 * silent connection, kept open and reused: keys from ssh-agent or ~/.ssh,
 * passwords only from an already unlocked vault. The user is never asked
 * anything: a host with a fingerprint to confirm or without credentials stays
 * without data, with the reason.
 */
public final class Metrics implements MetricsService {
    /** After an error, no retry before this, unless by hand: many failed logins trigger fail2ban. */
    private static final long PAUSE_AFTER_ERROR_MS = 60_000;
    /**
     * A remote command is closed after this time: a df on an unreachable NFS mount never ends, and its
     * thread would hold a slot among the {@link #set} forever (after 8 of those, no more metrics for anyone).
     */
    private static final long TIMEOUT_S = 30;
    /** The longest output accepted: a large nft list ruleset fits in a few hundred KB. */
    private static final int MAX_OUTPUT = 4 * 1024 * 1024;
    private static final ScheduledExecutorService ALARM = Executors.newSingleThreadScheduledExecutor(r -> {
        Thread t = new Thread(r, "metrics-deadlines");
        t.setDaemon(true);
        return t;
    });
    static final String COMMAND = String.join("; ",
            "echo \"N $(nproc 2>/dev/null || grep -c ^processor /proc/cpuinfo)\"",
            "echo \"C $(head -1 /proc/stat)\"",
            "sleep 1",
            "echo \"C $(head -1 /proc/stat)\"",
            "awk '/^MemTotal:|^MemAvailable:/{print \"M\",$1,$2}' /proc/meminfo",
            "echo \"U $(cut -d' ' -f1 /proc/uptime)\"",
            // On the server too df has a deadline, if the timeout command exists: a dead mount leaves no hanging processes.
            "T=$(command -v timeout >/dev/null 2>&1 && echo 'timeout 10')",
            "($T df -P -k -x tmpfs -x devtmpfs -x squashfs -x overlay -x efivarfs 2>/dev/null || $T df -P -k) | awk 'NR>1{print \"D\",$2,$3,$4,$6}'");

    /**
     * Pending updates ("A packages;security") and reboot ("R yes packages" or "R no"; nothing when the system
     * cannot tell). Only reads what the package manager already knows: it never downloads the lists.
     */
    static final String UPDATES = String.join("\n",
            "T=$(command -v timeout >/dev/null 2>&1 && echo 'timeout 25')",
            "if [ -x /usr/lib/update-notifier/apt-check ]; then echo \"A $($T /usr/lib/update-notifier/apt-check 2>&1 | tail -1)\"",
            "elif command -v apt-get >/dev/null 2>&1; then I=$($T apt-get -s -o Debug::NoLocking=true upgrade 2>/dev/null | grep '^Inst');"
                    + " echo \"A $(printf '%s' \"$I\" | grep -c .);$(printf '%s' \"$I\" | grep -ci security)\"",
            "elif command -v dnf >/dev/null 2>&1; then echo \"A $($T dnf -q --cacheonly check-update 2>/dev/null | grep -c '^[[:alnum:]]');"
                    // One line per advisory and version: the packages are counted, by name.
                    + "$($T dnf -q --cacheonly updateinfo list --security 2>/dev/null | awk '{n=$NF; sub(/-[^-]+-[^-]+$/, \"\", n); print n}' | sort -u | grep -c .)\"",
            "elif command -v yum >/dev/null 2>&1; then echo \"A $($T yum -q -C check-update 2>/dev/null | grep -c '^[[:alnum:]]');\"",
            // openSUSE: "v |" lines are updates, a "needed" security patch counts as one.
            "elif command -v zypper >/dev/null 2>&1; then echo \"A $($T zypper -q -n --no-refresh list-updates 2>/dev/null | grep -c '^v ');"
                    + "$($T zypper -q -n --no-refresh list-patches --category security 2>/dev/null | grep -ci '| needed')\"",
            "elif command -v apk >/dev/null 2>&1; then echo \"A $($T apk -u list 2>/dev/null | grep -c .);\"",
            "elif command -v pacman >/dev/null 2>&1; then echo \"A $($T pacman -Qu 2>/dev/null | grep -c .);\"",
            "fi",
            "if [ -f /var/run/reboot-required ]; then echo \"R yes $(tr '\\n' ' ' < /var/run/reboot-required.pkgs 2>/dev/null)\"",
            "elif [ -d /var/lib/dpkg ]; then echo 'R no'",
            "elif command -v needs-restarting >/dev/null 2>&1; then if $T needs-restarting -r >/dev/null 2>&1; then echo 'R no'; else [ $? -eq 1 ] && echo 'R yes'; fi",
            // zypper: exit code 102 when a reboot is needed, 0 when not.
            "elif command -v zypper >/dev/null 2>&1; then $T zypper -q needs-rebooting >/dev/null 2>&1; case $? in 0) echo 'R no' ;; 102) echo 'R yes' ;; esac",
            "fi");
    /** Updates are counted again after this: the package lists change a few times a day at most. */
    private static final long UPDATES_FOR_MS = 10 * 60_000;

    private final Vault vault;
    /** hostId -> last reading, good or with the error. */
    private final Map<String, MetricsResult> lastOnes = new ConcurrentHashMap<>();
    private final Map<String, Session> sessions = new ConcurrentHashMap<>();
    /** hostId -> the jump hosts its open connection goes through: if they change, it is opened again. */
    private final Map<String, String> routes = new ConcurrentHashMap<>();
    private final Map<String, Object> blocks = new ConcurrentHashMap<>();
    /** hostId -> last failed connection: whoever comes right after gets the same error without retrying. */
    private final Map<String, Failure> failed = new ConcurrentHashMap<>();

    /** A connection that failed a moment ago: the same error is given again instead of retrying at once. */
    private record Failure(Exception error, long when) {
        void rethrow() throws JSchException, IOException {
            if (error instanceof JSchException j) throw j;
            if (error instanceof IOException io) throw io;
            throw new IllegalStateException(error);
        }
    }
    private final Set<String> running = ConcurrentHashMap.newKeySet();
    private final Semaphore set = new Semaphore(8);
    private final ExecutorService jobs = Executors.newVirtualThreadPerTaskExecutor();
    private final Map<String, Updates> updates = new ConcurrentHashMap<>();
    private final Set<String> updating = ConcurrentHashMap.newKeySet();
    /** hostId -> CPU usage of the latest samples. */
    private final Map<String, Deque<Double>> history = new ConcurrentHashMap<>();

    public Metrics(Vault vault) {
        this.vault = vault;
    }

    @Override
    public List<Double> cpuHistory(String hostId) {
        Deque<Double> d = history.get(hostId);
        if (d == null) return new ArrayList<>();
        synchronized (d) {
            return new ArrayList<>(d);
        }
    }

    @Override
    public MetricsResult last(String hostId) {
        return lastOnes.get(hostId);
    }

    @Override
    public void refresh(Host h, boolean force, Consumer<MetricsResult> outcome) {
        if (jobs.isShutdown() || h.state == HostStatus.OFFLINE) return;
        MetricsResult before = lastOnes.get(h.id);
        if (!force && before != null && before.error() != null && System.currentTimeMillis() - before.when() < PAUSE_AFTER_ERROR_MS) return;
        if (!running.add(h.id)) return;
        Host copy = h.copy();
        jobs.execute(() -> {
            MetricsResult e;
            try {
                set.acquire();
                try {
                    e = new MetricsResult(parse(run(copy, COMMAND)), null, System.currentTimeMillis());
                } finally {
                    set.release();
                }
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                e = new MetricsResult(null, reason(ex), System.currentTimeMillis());
            } catch (JSchException | IOException | RuntimeException | StackOverflowError ex) {
                // RuntimeException and StackOverflowError: the output comes from the server, parsing must survive anything.
                e = new MetricsResult(null, reason(ex), System.currentTimeMillis());
            }
            if (e.resources() != null && e.resources().cpuUsage() != null) {
                Deque<Double> d = history.computeIfAbsent(copy.id, k -> new ArrayDeque<>());
                synchronized (d) {
                    d.addLast(e.resources().cpuUsage());
                    while (d.size() > SAMPLES) d.removeFirst();
                }
            }
            // An error does not erase the last good reading, it only marks it as old.
            MetricsResult old = lastOnes.get(copy.id);
            if (e.resources() == null && old != null && old.resources() != null)
                e = new MetricsResult(old.resources(), e.error(), e.when());
            lastOnes.put(copy.id, e);
            running.remove(copy.id);
            outcome.accept(e);
        });
    }

    @Override
    public void updates(Host h, boolean force, Consumer<Updates> outcome) {
        Updates known = updates.get(h.id);
        if (!force && known != null && System.currentTimeMillis() - known.when() < UPDATES_FOR_MS) {
            outcome.accept(known);
            return;
        }
        if (jobs.isShutdown() || h.state == HostStatus.OFFLINE || !updating.add(h.id)) return;
        Host copy = h.copy();
        jobs.execute(() -> {
            Updates u;
            try {
                set.acquire();
                try {
                    u = parseUpdates(run(copy, UPDATES), System.currentTimeMillis());
                } finally {
                    set.release();
                }
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                u = new Updates(null, null, null, null, reason(ex), System.currentTimeMillis());
            } catch (JSchException | IOException | RuntimeException ex) {
                // RuntimeException: the output comes from the server, parsing must survive anything.
                u = new Updates(null, null, null, null, reason(ex), System.currentTimeMillis());
            }
            if (u.error() == null) updates.put(copy.id, u);
            updating.remove(copy.id);
            outcome.accept(u);
        });
    }

    @Override
    public boolean running(String hostId) {
        return running.contains(hostId);
    }

    @Override
    public void forget(String hostId) {
        Session s = sessions.remove(hostId);
        routes.remove(hostId);
        if (s != null) s.disconnect();
        lastOnes.remove(hostId);
        history.remove(hostId);
        failed.remove(hostId);
        updates.remove(hostId);
    }

    @Override
    public void stop() {
        jobs.shutdownNow();
        sessions.values().forEach(Session::disconnect);
    }

    /**
     * Runs a command on the host's silent connection (opening it if needed) and returns its output.
     * From a worker thread: blocks until the command ends.
     */
    private String run(Host h, String command) throws JSchException, IOException {
        Session s;
        // One lock per host: two readings at once must not open two connections.
        synchronized (blocks.computeIfAbsent(h.id, k -> new Object())) {
            s = sessions.get(h.id);
            if (s == null || !s.isConnected() || !sameAddress(s, h)) {
                if (s != null) s.disconnect();
                Failure f = failed.get(h.id);
                if (f != null && System.currentTimeMillis() - f.when() < 10_000) f.rethrow();
                try {
                    // Through the jump hosts too, each with its own saved password.
                    s = SshTransport.open(h, SilentPrompts::new, m -> {}, 10_000);
                } catch (JSchException | IOException e) {
                    failed.put(h.id, new Failure(e, System.currentTimeMillis()));
                    throw e;
                }
                failed.remove(h.id);
                sessions.put(h.id, s);
                if (h.jump != null) routes.put(h.id, h.jump);
                else routes.remove(h.id);
            }
        }
        ChannelExec c = (ChannelExec) s.openChannel("exec");
        // When the time is up the channel is closed: the reading ends and the slot among the 8 is freed.
        AtomicBoolean expired = new AtomicBoolean();
        ScheduledFuture<?> alarm = ALARM.schedule(() -> {
            expired.set(true);
            c.disconnect();
        }, TIMEOUT_S, TimeUnit.SECONDS);
        try {
            c.setCommand(command);
            InputStream in = c.getInputStream();
            c.connect(5_000);
            String output;
            try {
                output = SshTransport.readAtMost(in, MAX_OUTPUT);
            } catch (IOException e) {
                if (expired.get()) throw new IllegalStateException(I18n.t("metrics.err.timeout", TIMEOUT_S));
                throw e;
            }
            // Closed by the alarm, the output is cut in half: better an error than wrong data.
            if (expired.get()) throw new IllegalStateException(I18n.t("metrics.err.timeout", TIMEOUT_S));
            return output;
        } finally {
            alarm.cancel(false);
            c.disconnect();
        }
    }

    /** The open connection still goes where the host says, through the same jump hosts. */
    private boolean sameAddress(Session s, Host h) {
        return s.getHost().equals(h.address) && s.getPort() == h.port && s.getUserName().equals(h.user)
                && Objects.equals(routes.get(h.id), h.jump);
    }

    static Resources parse(String text) {
        int cpu = 0;
        List<long[]> rounds = new ArrayList<>();
        long memTot = 0, memFree = 0, enabled = 0;
        List<Disk> disks = new ArrayList<>();
        for (String line : text.split("\n")) {
            String[] p = line.trim().split("\\s+");
            try {
                switch (p[0]) {
                    case "N" -> cpu = Integer.parseInt(p[1]);
                    // "C cpu user nice system idle iowait irq softirq steal ..."
                    case "C" -> {
                        long[] v = new long[8];
                        for (int i = 0; i < 8 && i + 2 < p.length; i++) v[i] = Long.parseLong(p[i + 2]);
                        rounds.add(v);
                    }
                    case "M" -> {
                        if (p[1].equals("MemTotal:")) memTot = Long.parseLong(p[2]);
                        else if (p[1].equals("MemAvailable:")) memFree = Long.parseLong(p[2]);
                    }
                    case "U" -> enabled = (long) Double.parseDouble(p[1]);
                    case "D" -> {
                        if (p.length >= 5 && disks.stream().noneMatch(d -> d.dot().equals(p[4])))
                            disks.add(new Disk(p[4], Long.parseLong(p[1]), Long.parseLong(p[2]), Long.parseLong(p[3])));
                    }
                    default -> {}
                }
            } catch (RuntimeException e) {
                // Odd line (shell with a welcome message, different df): skipped.
            }
        }
        Double usage = null;
        if (rounds.size() == 2) {
            long tot = 0, idle = 0;
            for (int i = 0; i < 8; i++) tot += rounds.get(1)[i] - rounds.get(0)[i];
            idle = rounds.get(1)[3] + rounds.get(1)[4] - rounds.get(0)[3] - rounds.get(0)[4];
            if (tot > 0) usage = Math.max(0, Math.min(1, 1 - (double) idle / tot));
        }
        if (memTot == 0 && disks.isEmpty() && usage == null) throw new IllegalStateException(I18n.t("metrics.err.notLinux"));
        return new Resources(cpu, usage, memTot, memFree, disks, enabled);
    }

    /** Longest reason for a reboot that is kept. */
    static final int MAX_BECAUSE = 300;

    static Updates parseUpdates(String text, long when) {
        Integer packages = null, security = null;
        Boolean reboot = null;
        String because = null;
        for (String line : text.split("\n")) {
            String l = line.trim();
            try {
                if (l.startsWith("A ")) {
                    String[] p = l.substring(2).split(";", -1);
                    packages = Integer.parseInt(p[0].trim());
                    if (p.length > 1 && !p[1].isBlank()) security = Integer.parseInt(p[1].trim());
                } else if (l.equals("R no")) {
                    reboot = false;
                } else if (l.startsWith("R yes")) {
                    reboot = true;
                    // The packages that asked for it, as the server wrote them: shown in a label, so without
                    // controls or invisible characters, and cut (the file can be as long as the output).
                    String rest = l.substring(5).replaceAll("[\\p{Cc}\\p{Cf}\\p{Zl}\\p{Zp}]", "").trim();
                    if (rest.length() > MAX_BECAUSE) rest = rest.substring(0, MAX_BECAUSE).strip() + "…";
                    because = rest.isEmpty() ? null : rest;
                }
            } catch (RuntimeException e) {
                // apt-check may print a warning instead of the numbers: that line is skipped.
            }
        }
        return new Updates(packages, security, reboot, because, null, when);
    }

    public static String reason(Throwable e) {
        String m = e.getMessage() != null ? e.getMessage() : e.toString();
        if (m.contains("reject HostKey") || m.contains("HostKey has been changed"))
            return I18n.t("metrics.err.fingerprint");
        if (m.contains("Auth fail") || m.contains("Auth cancel"))
            return I18n.t("metrics.err.auth");
        if (e instanceof SshErrors.Explained || SshErrors.key(e) != null)
            return SshErrors.explain(e);
        return e instanceof JSchException ? m : I18n.t("metrics.err.other", m);
    }

    /** Answers without dialogs: new fingerprints rejected, passwords only from the vault and only once. */
    private final class SilentPrompts implements UserInfo, UIKeyboardInteractive {
        private final Host host;
        private boolean tried;

        SilentPrompts(Host host) {
            this.host = host;
        }

        private String saved() {
            if (tried) return null;
            tried = true;
            return vault.password(host.id);
        }

        private String password;

        @Override public String getPassphrase() { return null; }

        @Override public String getPassword() { return password; }

        @Override
        public boolean promptPassword(String message) {
            password = saved();
            return password != null;
        }

        @Override public boolean promptPassphrase(String message) { return false; }

        @Override public boolean promptYesNo(String message) { return false; }

        @Override public void showMessage(String message) {}

        @Override
        public String[] promptKeyboardInteractive(String destination, String name, String instructions, String[] questions, boolean[] echo) {
            if (questions.length == 0) return new String[0];
            // The saved password only to a password question: a 2FA code is never answered.
            if (questions.length != 1 || echo[0] || !SshTransport.passwordQuestion(questions[0])) return null;
            return promptPassword(questions[0]) ? new String[] {password} : null;
        }
    }
}
