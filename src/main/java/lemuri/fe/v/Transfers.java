package lemuri.fe.v;

import com.jcraft.jsch.ChannelSftp;
import com.jcraft.jsch.JSchException;
import com.jcraft.jsch.SftpException;
import java.io.File;
import java.io.IOException;
import java.util.ArrayList;
import java.util.EnumMap;
import java.util.HashSet;
import java.util.List;
import java.util.Map;
import java.util.Set;
import java.util.TreeSet;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.CopyOnWriteArrayList;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import java.util.concurrent.Semaphore;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicInteger;
import java.util.concurrent.atomic.AtomicLong;
import java.util.function.BooleanSupplier;
import java.util.function.Consumer;
import java.util.function.Supplier;
import javafx.animation.PauseTransition;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.DoubleProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.property.SimpleDoubleProperty;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;
import javafx.geometry.Pos;
import javafx.geometry.Side;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.CustomMenuItem;
import javafx.scene.control.Label;
import javafx.scene.control.MenuItem;
import javafx.scene.control.ProgressBar;
import javafx.scene.control.SeparatorMenuItem;
import javafx.scene.control.Tooltip;
import javafx.scene.input.DragEvent;
import javafx.scene.input.TransferMode;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Region;
import javafx.stage.DirectoryChooser;
import javafx.stage.FileChooser;
import javafx.util.Duration;
import lemuri.be.terminal.SftpFiles;
import lemuri.be.terminal.SshErrors;
import lemuri.be.terminal.SshTransport;
import lemuri.be.util.I18n;
import lemuri.be.util.RemoteNames;
import lemuri.fe.m.Session;
import lemuri.fe.v.terminal.Terminal;

/**
 * File upload and download via SFTP, on the session connection, each on a channel of its own. They start as
 * soon as they are confirmed, side by side: one does not wait for the others to finish, except beyond
 * {@link #PER_HOST} on the same host (the server allows few channels per connection, and the shell has one).
 * The status bar shows the transfer, or the total of all of them with a list to cancel each; at the end a
 * summary, which stays until closed if something failed.
 */
public final class Transfers {
    private Transfers() {}

    /** Transfers running at once on one host; the next ones wait for a free place. */
    static final int PER_HOST = 4;
    private static final Slots SLOTS = new Slots(PER_HOST);
    private static final ExecutorService workers = Executors.newCachedThreadPool(r -> {
        Thread t = new Thread(r, "transfers");
        t.setDaemon(true);
        return t;
    });
    /** Transfers waiting or running, in the order they were asked. */
    private static final List<Transfer> active = new CopyOnWriteArrayList<>();
    /** What has ended since the last summary was closed: the summary is made of these (JavaFX thread only). */
    private static final List<Outcome> finished = new ArrayList<>();
    /**
     * Where the running transfers write: a host and a server path, or a path on this computer. A second one to
     * the same place would overwrite the first without asking, so it is refused until the first ends.
     */
    private static final Set<String> busy = new HashSet<>();

    private static final StringProperty text = new SimpleStringProperty("");
    /** From 0 to 1; negative when nothing is running (the bar disappears). */
    private static final DoubleProperty quota = new SimpleDoubleProperty(-1);
    private static final BooleanProperty visible = new SimpleBooleanProperty(false);
    private static final BooleanProperty error = new SimpleBooleanProperty(false);
    /** The button: cancel the transfer, open the list of all of them, or close a summary with errors. */
    private static final BooleanProperty closable = new SimpleBooleanProperty(false);
    private static final StringProperty closeTip = new SimpleStringProperty("");
    private static PauseTransition turnOff;
    private static ContextMenu list;
    private static Node anchor;

    enum Kind { DONE, FAILED, CANCELLED }

    /** How a transfer ended: {@code message} alone, or in a summary {@code what} and {@code detail}. */
    record Outcome(String what, Kind kind, String message, String detail) {
        static Outcome note(String message, boolean bad) {
            return new Outcome(null, bad ? Kind.FAILED : Kind.DONE, message, message);
        }
    }

    /** For the status bar: name, percentage, bar and button; visible only during transfers and shortly after. */
    public static Node indicator() {
        Label l = new Label();
        l.textProperty().bind(text);
        l.getStyleClass().add("status-item");
        l.setMinWidth(Region.USE_PREF_SIZE);
        Tooltip full = new Tooltip();
        full.textProperty().bind(text);
        l.setTooltip(full);
        l.setOnMouseClicked(e -> {
            if (!active.isEmpty()) showList();
        });
        error.addListener((o, before, now) -> {
            if (now) l.getStyleClass().add("transfer-error");
            else l.getStyleClass().remove("transfer-error");
        });
        ProgressBar b = new ProgressBar();
        b.progressProperty().bind(quota);
        b.getStyleClass().add("transfer-bar");
        b.visibleProperty().bind(quota.greaterThanOrEqualTo(0));
        b.managedProperty().bind(b.visibleProperty());
        Button x = Ui.action("close", "", Transfers::closeButton);
        x.getTooltip().textProperty().bind(closeTip);
        x.getStyleClass().add("transfer-cancel");
        x.visibleProperty().bind(closable);
        x.managedProperty().bind(closable);
        HBox h = new HBox(4, l, b, x);
        h.setAlignment(Pos.CENTER_LEFT);
        h.visibleProperty().bind(visible);
        h.managedProperty().bind(visible);
        anchor = h;
        return h;
    }

    /**
     * Upload of files (or folders, with all their content) into the server
     * folder. It asks for confirmation first, saying whether something will be overwritten.
     */
    public static void load(Session s, List<File> file, String folder, Runnable after) {
        if (file == null || file.isEmpty()) return;
        if (!(s.transport instanceof SshTransport ssh)) {
            message(I18n.t("transfer.upload.onlySsh"), true);
            return;
        }
        String host = hostKey(s);
        List<String> targets = file.stream().map(f -> host + ":" + SftpFiles.join(folder, f.getName())).toList();
        String what = file.size() == 1 ? "\"" + file.get(0).getName() + "\"" + (file.get(0).isDirectory() ? " " + I18n.t("transfer.folderMark") : "")
                : I18n.t("files.items", file.size());
        if (taken(targets)) {
            message(I18n.t("transfer.busy", what), true);
            return;
        }
        // What is already there is checked before the question; on a far server it takes a moment (a channel to
        // open, the folder to read): the status bar says so meanwhile, if it is not showing transfers.
        checking(I18n.t("transfer.checking", folder, s.host.name));
        Thread t = new Thread(() -> {
            List<String> exist;
            try {
                ChannelSftp open = ssh.openSftp();
                try {
                    exist = SftpFiles.existing(open, folder, file);
                } finally {
                    open.disconnect();
                }
            } catch (JSchException ex) {
                Platform.runLater(() -> message(I18n.t("transfer.upload.failed", explain(ex)), true));
                return;
            }
            Platform.runLater(() -> {
                checked();
                String text = I18n.t("transfer.upload.ask", what, folder, s.host.destination())
                        + (exist.isEmpty() ? "" : "\n\n" + I18n.t("transfer.upload.overwrites", String.join(", ", exist)));
                if (!Dialogs.confirm(I18n.t("transfer.upload"), text, exist.isEmpty() ? I18n.t("transfer.upload") : I18n.t("transfer.overwrite"))) return;
                start(ssh, new Transfer("↑", what, host, targets), (k, c) -> {
                    // While it waited, another transfer or someone on the server may have put files with these names there.
                    List<String> now = new ArrayList<>(SftpFiles.existing(c, folder, file));
                    now.removeAll(exist);
                    if (!now.isEmpty() && !askAgain(I18n.t("transfer.upload"), I18n.t("transfer.upload.nowThere", folder, String.join(", ", now)), I18n.t("transfer.overwrite")))
                        k.cancelled = true;
                    if (k.cancelled) return I18n.t("transfer.upload.cancelled");
                    long total = 0;
                    for (File f : file) total += SftpFiles.localSize(f);
                    k.total(total);
                    for (File f : file) {
                        SftpFiles.upload(c, f, folder, k);
                        if (k.cancelled) break;
                    }
                    return k.cancelled ? I18n.t("transfer.upload.cancelled") : I18n.t("transfer.uploaded", what, folder) + k.skippedReported(I18n.t("transfer.skipped.links"));
                }, after);
            });
        }, "transfers-check");
        t.setDaemon(true);
        t.start();
    }

    /** Picks files from the computer and uploads them into the server folder. */
    public static void chooseAndUpload(Session s, String folder, Runnable after) {
        String title = I18n.t("transfer.uploadTo", folder);
        // The desktop's own chooser first (see PortalFiles); the JavaFX one where there is no portal.
        if (PortalFiles.openFiles(title, true, files -> load(s, files.isEmpty() ? null : files, folder, after))) return;
        FileChooser fc = new FileChooser();
        fc.setTitle(title);
        load(s, fc.showOpenMultipleDialog(Dialogs.main), folder, after);
    }

    /**
     * Upload into the folder the shell is in, which the terminal reports;
     * then confirmation and queue as usual. {@code file} null: they are
     * picked from the computer.
     */
    public static void uploadFromTerminal(Session s, List<File> file) {
        if (s == null || !(s.transport instanceof SshTransport)) {
            message(I18n.t("transfer.upload.onlyTerminal"), true);
            return;
        }
        shellFolder(s, folder -> {
            if (file == null) chooseAndUpload(s, folder, null);
            else load(s, file, folder, null);
        });
    }

    /**
     * The folder the shell of {@code s} is in, as the terminal reports it; then {@code then} on the JavaFX thread.
     * Without commands typed into the terminal it cannot be asked: the home folder is used.
     */
    private static void shellFolder(Session s, Consumer<String> then) {
        if (!Terminal.typesCommands()) {
            SshTransport ssh = (SshTransport) s.transport;
            Thread.ofVirtual().name("sftp-home").start(() -> {
                String home;
                try {
                    ChannelSftp c = ssh.openSftp();
                    try {
                        home = c.getHome();
                    } finally {
                        c.disconnect();
                    }
                } catch (JSchException | SftpException e) {
                    Platform.runLater(() -> message(I18n.t("transfer.noFolder"), true));
                    return;
                }
                Platform.runLater(() -> then.accept(home));
            });
            return;
        }
        SessionViews.terminal(s).currentFolder(folder -> {
            if (folder == null) message(I18n.t("transfer.noFolder"), true);
            else then.accept(folder);
        });
    }

    /**
     * Download of a name the user right-clicked in the output of an {@code ls}: it is in the shell's folder, or in
     * the folder that ls was given ({@code ls /var/log}). The folder on this computer is asked, then the queue.
     */
    public static void downloadFromTerminal(Session s, String name, String lsCommand) {
        if (s == null || !(s.transport instanceof SshTransport ssh)) {
            message(I18n.t("transfer.upload.onlyTerminal"), true);
            return;
        }
        shellFolder(s, folder -> {
            String title = I18n.t("transfer.downloadTo", name);
            Consumer<File> into = local -> {
                if (local != null) fetch(s, ssh, folder, Terminal.lsFolder(lsCommand), name, local);
            };
            if (PortalFiles.openFolder(title, into)) return;
            DirectoryChooser dc = new DirectoryChooser();
            dc.setTitle(title);
            into.accept(dc.showDialog(Dialogs.main));
        });
    }

    /** Finds the name on the server, asks before replacing something here, and starts the download. */
    private static void fetch(Session s, SshTransport ssh, String shell, String listed, String name, File local) {
        checking(I18n.t("transfer.checking", shell, s.host.name));
        Thread t = new Thread(() -> {
            String path;
            try {
                ChannelSftp open = ssh.openSftp();
                try {
                    path = remotePath(open, shell, listed, name);
                } catch (SftpException missing) {
                    Platform.runLater(() -> message(I18n.t("transfer.download.missing", name), true));
                    return;
                } finally {
                    open.disconnect();
                }
            } catch (JSchException ex) {
                Platform.runLater(() -> message(I18n.t("transfer.download.failed", explain(ex)), true));
                return;
            }
            String what = SftpFiles.lastName(path);
            Platform.runLater(() -> {
                checked();
                if (!SftpFiles.safeLocalName(what)) {
                    message(I18n.t("transfer.download.badName", what), true);
                    return;
                }
                File target = new File(local, what);
                List<String> targets = List.of("local:" + target.getAbsolutePath());
                if (taken(targets)) {
                    message(I18n.t("transfer.busy", "\"" + what + "\""), true);
                    return;
                }
                boolean replace = target.exists();
                if (replace && !Dialogs.confirm(I18n.t("transfer.download"), I18n.t("transfer.download.replace", target), I18n.t("transfer.overwrite"))) return;
                start(ssh, new Transfer("↓", "\"" + what + "\"", hostKey(s), targets), (k, c) -> {
                    // It did not exist when asked, but something has put it there while this one waited.
                    if (!replace && target.exists() && !askAgain(I18n.t("transfer.download"), I18n.t("transfer.download.replace", target), I18n.t("transfer.overwrite")))
                        k.cancelled = true;
                    if (k.cancelled) return I18n.t("transfer.download.cancelled");
                    k.total(SftpFiles.remoteSize(c, path));
                    SftpFiles.download(c, path, local, k);
                    return k.cancelled ? I18n.t("transfer.download.cancelled") : I18n.t("transfer.downloaded", what, local) + k.skippedReported(I18n.t("transfer.skipped.links"));
                }, null);
            });
        }, "transfers-check");
        t.setDaemon(true);
        t.start();
    }

    /** Where the name is on the server: in the folder ls listed if there is such a thing, otherwise in the shell's folder. */
    private static String remotePath(ChannelSftp c, String shell, String listed, String name) throws SftpException {
        if (listed != null) {
            String base = listed.equals("~") ? c.getHome() : listed.startsWith("~/") ? SftpFiles.join(c.getHome(), listed.substring(2))
                    : listed.startsWith("/") ? listed : SftpFiles.join(shell, listed);
            String candidate = SftpFiles.join(base, name);
            try {
                c.lstat(RemoteNames.literal(candidate));
                return candidate;
            } catch (SftpException notThere) {
                // ls was given a file, or a pattern: the name is in the shell's folder
            }
        }
        String path = SftpFiles.join(shell, name);
        c.lstat(RemoteNames.literal(path));
        return path;
    }

    /** Files dragged from the computer onto a terminal: they go into its shell's folder. */
    public static void allowUpload(Node zone, Supplier<Session> session) {
        // Filters: the terminal WebView would handle the drag its own way.
        zone.addEventFilter(DragEvent.DRAG_OVER, e -> {
            Session s = session.get();
            if (!e.getDragboard().hasFiles() || s == null) return;
            e.acceptTransferModes(TransferMode.COPY);
            e.consume();
        });
        zone.addEventFilter(DragEvent.DRAG_DROPPED, e -> {
            Session s = session.get();
            if (!e.getDragboard().hasFiles() || s == null) return;
            List<File> file = List.copyOf(e.getDragboard().getFiles());
            e.setDropCompleted(true);
            e.consume();
            // After the drop: the confirmation dialog must not open during the drag.
            Platform.runLater(() -> uploadFromTerminal(s, file));
        });
    }

    private interface Work {
        String run(Transfer k, ChannelSftp c) throws JSchException, SftpException, IOException;
    }

    /** The host a transfer counts against for {@link #PER_HOST}. */
    private static String hostKey(Session s) {
        return s.host.id != null ? s.host.id : s.host.destination();
    }

    /** True if a running transfer writes to one of {@code targets}. */
    private static boolean taken(List<String> targets) {
        synchronized (busy) {
            return targets.stream().anyMatch(busy::contains);
        }
    }

    /**
     * Starts a confirmed transfer on a thread of its own (JavaFX thread). It waits only for a free place on its
     * host, then opens its own channel: nothing is kept open while waiting.
     */
    private static void start(SshTransport ssh, Transfer t, Work w, Runnable after) {
        synchronized (busy) {
            // Two questions answered one after the other for the same place: only the first goes.
            if (t.targets.stream().anyMatch(busy::contains)) {
                message(I18n.t("transfer.busy", t.what), true);
                return;
            }
            busy.addAll(t.targets);
        }
        if (active.isEmpty() && !error.get()) finished.clear();
        active.add(t);
        t.update(true);
        workers.execute(() -> {
            Outcome o;
            boolean slot = false;
            try {
                slot = SLOTS.take(t.host, () -> t.cancelled);
                if (!slot) {
                    o = new Outcome(t.what, Kind.CANCELLED, I18n.t("transfer.cancelled"), null);
                } else {
                    t.started = true;
                    t.update(true);
                    ChannelSftp c = ssh.openSftp();
                    try {
                        String m = w.run(t, c);
                        o = new Outcome(t.what, t.cancelled ? Kind.CANCELLED : Kind.DONE, m, null);
                    } finally {
                        c.disconnect();
                    }
                }
            } catch (JSchException | SftpException | IOException | IllegalArgumentException | IllegalStateException ex) {
                // IllegalArgument/IllegalState: a remote name refused as unsafe, or a folder that cannot be created.
                o = new Outcome(t.what, Kind.FAILED, I18n.t("transfer.failed", explain(ex)), explain(ex));
            } catch (InterruptedException ex) {
                Thread.currentThread().interrupt();
                o = new Outcome(t.what, Kind.CANCELLED, I18n.t("transfer.cancelled"), null);
            } finally {
                if (slot) SLOTS.give(t.host);
                synchronized (busy) {
                    t.targets.forEach(busy::remove);
                }
            }
            Outcome end = o;
            Platform.runLater(() -> {
                active.remove(t);
                finished.add(end);
                refresh();
                if (after != null) after.run();
            });
        });
    }

    /** Asks the user from a transfer thread, and waits for the answer. */
    private static boolean askAgain(String title, String text, String action) {
        CompletableFuture<Boolean> answer = new CompletableFuture<>();
        Platform.runLater(() -> answer.complete(Dialogs.confirm(title, text, action)));
        return answer.join();
    }

    /** The ✕ of the status bar: one transfer is cancelled, several open their list, a summary is closed. */
    private static void closeButton() {
        if (active.size() == 1) active.get(0).cancelled = true;
        else if (!active.isEmpty()) showList();
        else {
            finished.clear();
            visible.set(false);
            refresh();
        }
    }

    /** Every transfer with its bar and its ✕, and "Cancel all"; it follows them while it is open. */
    private static void showList() {
        if (list != null) list.hide();
        list = new ContextMenu();
        list.getStyleClass().add("transfer-list");
        fill(list);
        list.show(anchor, Side.TOP, 0, 0);
    }

    private static void fill(ContextMenu m) {
        m.getItems().clear();
        for (Transfer t : active) {
            Label l = new Label();
            l.textProperty().bind(t.line);
            l.setMinWidth(Region.USE_PREF_SIZE);
            ProgressBar b = new ProgressBar();
            b.progressProperty().bind(t.progress);
            b.getStyleClass().add("transfer-bar");
            Button x = Ui.action("close", I18n.t("transfer.cancel"), () -> t.cancelled = true);
            x.getStyleClass().add("transfer-cancel");
            HBox row = new HBox(8, l, Ui.space(), b, x);
            row.setAlignment(Pos.CENTER_LEFT);
            row.setUserData(t);
            m.getItems().add(new CustomMenuItem(row, false));
        }
        m.getItems().add(new SeparatorMenuItem());
        MenuItem all = new MenuItem(I18n.t("transfer.cancelAll"));
        all.setOnAction(e -> active.forEach(t -> t.cancelled = true));
        m.getItems().add(all);
    }

    /** The open list shows the transfers that are running now: rebuilt when one starts or ends. */
    private static void followList() {
        if (list == null || !list.isShowing()) return;
        if (active.size() < 2) {
            list.hide();
            return;
        }
        List<Object> shown = list.getItems().stream().filter(i -> i instanceof CustomMenuItem).map(i -> ((CustomMenuItem) i).getContent().getUserData()).toList();
        if (!shown.equals(List.copyOf(active))) fill(list);
    }

    /** "Checking…" while the server is asked what is there, only if no transfer is being shown. */
    private static void checking(String m) {
        if (!active.isEmpty() || !finished.isEmpty()) return;
        show(m, -1, false);
        closable.set(false);
    }

    /** The check is over: the status bar goes back to what it showed. */
    private static void checked() {
        if (active.isEmpty() && finished.isEmpty()) visible.set(false);
    }

    /** A message of its own (a refusal, a check that failed): with transfers running, it waits for the summary. */
    private static void message(String m, boolean bad) {
        if (active.isEmpty() && !error.get()) finished.clear();
        finished.add(Outcome.note(m, bad));
        refresh();
    }

    /** What the status bar shows now: the transfers, or the summary of those that ended (JavaFX thread). */
    private static void refresh() {
        followList();
        if (!active.isEmpty()) {
            if (turnOff != null) turnOff.stop();
            if (active.size() == 1) {
                Transfer t = active.get(0);
                show(t.line.get(), t.progress.get(), false);
                closeTip.set(I18n.t("transfer.cancel"));
            } else {
                long done = 0, total = 0;
                Set<String> arrows = new TreeSet<>();
                for (Transfer t : active) {
                    arrows.add(t.arrow);
                    if (t.started) {
                        done += t.done.get();
                        total += t.total;
                    }
                }
                double q = total == 0 ? 0 : Math.min(1, done / (double) total);
                show(String.join("", arrows) + " " + I18n.t("transfer.many", active.size()) + " · " + (int) (q * 100) + "% · " + measure(done) + " / " + measure(total), q, false);
                closeTip.set(I18n.t("transfer.showAll"));
            }
            closable.set(true);
            return;
        }
        if (finished.isEmpty()) {
            visible.set(false);
            return;
        }
        boolean bad = finished.stream().anyMatch(o -> o.kind() == Kind.FAILED);
        show(summary(finished), -1, bad);
        closeTip.set(I18n.t("transfer.close"));
        closable.set(bad);
        if (turnOff != null) turnOff.stop();
        // A summary with errors stays until it is closed; otherwise it goes after a few seconds.
        if (bad) return;
        turnOff = new PauseTransition(Duration.seconds(5));
        turnOff.setOnFinished(e -> {
            if (!active.isEmpty()) return;
            finished.clear();
            visible.set(false);
        });
        turnOff.play();
    }

    /** One outcome as it is; several as counts, then what failed and why. */
    static String summary(List<Outcome> outcomes) {
        if (outcomes.size() == 1) return outcomes.get(0).message();
        Map<Kind, Integer> n = new EnumMap<>(Kind.class);
        for (Outcome o : outcomes) n.merge(o.kind(), 1, Integer::sum);
        List<String> parts = new ArrayList<>();
        if (n.containsKey(Kind.DONE)) parts.add(I18n.t("transfer.summary.done", n.get(Kind.DONE)));
        if (n.containsKey(Kind.FAILED)) parts.add(I18n.t("transfer.summary.failed", n.get(Kind.FAILED)));
        if (n.containsKey(Kind.CANCELLED)) parts.add(I18n.t("transfer.summary.cancelled", n.get(Kind.CANCELLED)));
        String head = String.join(" · ", parts);
        List<String> why = outcomes.stream().filter(o -> o.kind() == Kind.FAILED).map(o -> o.what() != null ? o.what() + ": " + o.detail() : o.detail()).toList();
        return why.isEmpty() ? head : head + " · " + String.join("; ", why);
    }

    private static void show(String m, double q, boolean bad) {
        if (turnOff != null) turnOff.stop();
        text.set(m);
        quota.set(q);
        error.set(bad);
        visible.set(true);
    }

    /**
     * Places for transfers, {@code perHost} for each host. Taken in the order asked (a fair semaphore), and
     * given up by a transfer cancelled while it waits.
     */
    static final class Slots {
        private final int perHost;
        private final Map<String, Semaphore> hosts = new ConcurrentHashMap<>();

        Slots(int perHost) {
            this.perHost = perHost;
        }

        /** Waits for a free place on {@code host}: false if {@code cancelled} becomes true meanwhile. */
        boolean take(String host, BooleanSupplier cancelled) throws InterruptedException {
            Semaphore s = hosts.computeIfAbsent(host, h -> new Semaphore(perHost, true));
            while (!s.tryAcquire(200, TimeUnit.MILLISECONDS)) {
                if (cancelled.getAsBoolean()) return false;
            }
            return true;
        }

        void give(String host) {
            hosts.get(host).release();
        }
    }

    /** A transfer waiting or running: what it moves, how far it is, whether the user cancelled it. */
    private static final class Transfer implements SftpFiles.Watch {
        final String arrow;
        final String what;
        final String host;
        final List<String> targets;
        volatile boolean cancelled;
        volatile boolean started;
        final AtomicInteger skipped = new AtomicInteger();
        final AtomicLong done = new AtomicLong();
        volatile long total = 1;
        volatile String name = "";
        long last;
        /** For its row in the list and for the status bar when it is the only one (JavaFX thread). */
        final StringProperty line = new SimpleStringProperty("");
        final DoubleProperty progress = new SimpleDoubleProperty(0);

        Transfer(String arrow, String what, String host, List<String> targets) {
            this.arrow = arrow;
            this.what = what;
            this.host = host;
            this.targets = targets;
        }

        /** The size is known only once it has started (for a folder download, after listing it). */
        void total(long bytes) {
            total = Math.max(bytes, 1);
        }

        String skippedReported(String why) {
            int n = skipped.get();
            return n == 0 ? "" : " " + I18n.t("transfer.skipped", n, why);
        }

        @Override
        public boolean cancelled() {
            return cancelled;
        }

        @Override
        public void skipped() {
            skipped.incrementAndGet();
        }

        @Override
        public void file(String n) {
            name = n;
        }

        @Override
        public void init(int op, String from, String a, long max) {
            update(true);
        }

        @Override
        public boolean count(long n) {
            done.addAndGet(n);
            update(false);
            return !cancelled;
        }

        @Override
        public void end() {
            update(true);
        }

        /** Its line and bar, at most ten times per second. */
        void update(boolean immediately) {
            long now = System.currentTimeMillis();
            if (!immediately && now - last < 100) return;
            last = now;
            String m;
            double q;
            if (!started) {
                m = arrow + " " + what + " · " + I18n.t("transfer.waiting");
                q = 0;
            } else {
                long d = done.get();
                q = Math.min(1, d / (double) total);
                m = arrow + " " + (name.isEmpty() ? what : name) + " · " + (int) (q * 100) + "% · " + measure(d) + " / " + measure(total);
            }
            Platform.runLater(() -> {
                line.set(m);
                progress.set(q);
                refresh();
            });
        }
    }

    private static String explain(Exception ex) {
        return SshErrors.explain(ex);
    }

    private static String measure(long b) {
        if (b < 1024) return b + " B";
        if (b < 1024 * 1024) return String.format("%.1f KB", b / 1024.0);
        if (b < 1024L * 1024 * 1024) return String.format("%.1f MB", b / (1024.0 * 1024));
        return String.format("%.2f GB", b / (1024.0 * 1024 * 1024));
    }
}
