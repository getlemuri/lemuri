package lemuri.fe.v;

import java.lang.reflect.Field;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.concurrent.Callable;
import java.util.concurrent.TimeUnit;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Supplier;
import java.util.prefs.BackingStoreException;
import java.util.prefs.Preferences;
import javafx.application.Platform;
import javafx.event.Event;
import javafx.geometry.Bounds;
import javafx.scene.Node;
import javafx.scene.Parent;
import javafx.scene.Scene;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBase;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.DialogPane;
import javafx.scene.control.Labeled;
import javafx.scene.control.Menu;
import javafx.scene.control.MenuItem;
import javafx.scene.control.TableView;
import javafx.scene.control.TextInputControl;
import javafx.scene.input.ContextMenuEvent;
import javafx.scene.input.PickResult;
import javafx.scene.layout.Pane;
import javafx.scene.text.Text;
import javafx.stage.Stage;
import javafx.stage.Window;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeAll;
import org.junit.jupiter.api.BeforeEach;
import org.testfx.api.FxRobot;
import org.testfx.api.FxToolkit;
import org.testfx.util.WaitForAsyncUtils;
import lemuri.App;
import lemuri.be.dto.Disk;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.dto.MetricsResult;
import lemuri.be.dto.Resources;
import lemuri.be.dto.SessionStatus;
import lemuri.be.dto.Updates;
import lemuri.be.service.HostRepository;
import lemuri.be.service.MetricsService;
import lemuri.be.service.ProbeService;
import lemuri.be.service.Transport;
import lemuri.be.service.Vault;
import lemuri.be.sharing.Sharing;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Session;
import lemuri.fe.v.terminal.Terminal;

/**
 * Base of the interface tests: the real main window (Console) on Monocle's headless screen, driven with
 * TestFX, over a made-up world: hosts and vault in memory, metrics and probe that answer at once, sessions
 * whose transports print a prompt and record what they are sent. Nothing touches the network or ~/.lemuri.
 */
abstract class UiTest {
    protected final FxRobot robot = new FxRobot();
    protected AppContext app;
    protected Console console;
    protected Stage stage;
    protected MemoryVault vault;
    protected FakeMetrics metrics;
    /** The world's sharing engine (keys in its vault, local network off). */
    protected Sharing sharing;
    protected final List<FakeTransport> transports = new ArrayList<>();

    @BeforeAll
    static void toolkit() throws Exception {
        FxToolkit.registerPrimaryStage();
    }

    /** The hosts the world starts with; tests may override. */
    protected List<Host> hosts() {
        return List.of(host("web-01", "Production/EU", "203.0.113.21"), host("db-01", "Production/EU", "203.0.113.20"),
                host("api-us-1", "Production/US", "203.0.113.40"), host("homelab", "Personal", "192.0.2.10"));
    }

    @BeforeEach
    void world() throws Exception {
        // Preferences of an earlier test (theme, hidden tree, terminal font) must not leak into this one.
        // Emptied, not removed: the app keeps its preference nodes in static fields.
        clear(Preferences.userRoot().node("lemuri"));
        vault = new MemoryVault();
        metrics = new FakeMetrics();
        transports.clear();
        List<Host> start = new ArrayList<>();
        for (Host h : hosts()) start.add(h.copy());
        HostRepository repository = new MemoryRepository(start);
        stage = FxToolkit.registerPrimaryStage();
        on(() -> {
            sharing = new Sharing(vault);
            app = AppContext.withTransports(repository, vault, metrics, new FakeProbe(), sharing, h -> {
                FakeTransport t = new FakeTransport(h);
                transports.add(t);
                return t;
            });
            SessionViews.attach(app);
            console = new Console(app);
            Scene scene = new Scene(console, 1400, 900);
            scene.getStylesheets().add(App.class.getResource("style.css").toExternalForm());
            stage.setScene(scene);
            stage.show();
            Dialogs.main = stage;
            app.monitor.launch();
            app.sharing.onArrival = i -> SharingDialogs.propose(app, i);
            // As the app does at startup: the sharing keys are made (local network stays off in the test preferences).
            app.sharing.restart();
        });
        until(() -> sharing.myKeyCard() != null, "the sharing keys");
        settle();
    }

    @AfterEach
    void close() throws Exception {
        on(() -> {
            if (console.ghost().active()) console.ghost().leave();
            for (Window w : new ArrayList<>(Window.getWindows())) if (w != stage) w.hide();
            app.stop();
            // Off the scene, as when the app leaves a view: timers of the views (the summary's) stop.
            stage.getScene().setRoot(new Pane());
        });
        FxToolkit.cleanupStages();
    }

    // ── Helpers ────────────────────────────────────────────────────────────

    private static void clear(Preferences node) throws BackingStoreException {
        node.clear();
        for (String child : node.childrenNames()) clear(node.node(child));
    }

    static Host host(String name, String group, String address) {
        Host h = new Host();
        h.id = name;
        h.name = name;
        h.group = group;
        h.address = address;
        h.user = "deploy";
        h.state = HostStatus.ONLINE;
        h.os = "Ubuntu 24.04 LTS";
        return h;
    }

    /** Runs on the JavaFX thread and waits for it. */
    protected static void on(Runnable r) {
        try {
            WaitForAsyncUtils.asyncFx(r).get(10, TimeUnit.SECONDS);
        } catch (Exception e) {
            throw new AssertionError(e);
        }
    }

    protected static <T> T get(Callable<T> c) {
        try {
            return WaitForAsyncUtils.asyncFx(c).get(10, TimeUnit.SECONDS);
        } catch (Exception e) {
            throw new AssertionError(e);
        }
    }

    /** Lets the JavaFX thread finish what is queued (layouts, runLater, animations started). */
    protected static void settle() {
        WaitForAsyncUtils.waitForFxEvents();
    }

    /** Waits until {@code condition} is true, up to 5 seconds. */
    protected static void until(Supplier<Boolean> condition, String what) {
        long end = System.currentTimeMillis() + 5000;
        while (System.currentTimeMillis() < end) {
            settle();
            if (get(condition::get)) return;
            WaitForAsyncUtils.sleep(50, TimeUnit.MILLISECONDS);
        }
        throw new AssertionError("timed out waiting for: " + what);
    }

    /** The nodes matching {@code query} in every open window (dialogs and menus are windows of their own). */
    protected List<Node> all(String query) {
        return get(() -> {
            List<Node> out = new ArrayList<>();
            for (Window w : Window.getWindows()) if (w.isShowing() && w.getScene() != null) out.addAll(w.getScene().getRoot().lookupAll(query));
            return out;
        });
    }

    /** The labelled control (button, label, menu item text) showing exactly {@code text}, in any window. */
    protected Node text(String text) {
        return robot.lookup(n -> n instanceof Labeled l && text.equals(l.getText()) && n.isVisible()).query();
    }

    protected boolean shows(String text) {
        return get(() -> {
            for (Window w : Window.getWindows()) {
                if (!w.isShowing() || w.getScene() == null) continue;
                if (contains(w.getScene().getRoot(), text)) return true;
            }
            return false;
        });
    }

    private static boolean contains(Node n, String text) {
        if (!n.isVisible()) return false;
        if (n instanceof Labeled l && l.getText() != null && l.getText().contains(text)) return true;
        if (n instanceof Text t && t.getText() != null && t.getText().contains(text)) return true;
        if (n instanceof TextInputControl t && t.getText() != null && t.getText().contains(text)) return true;
        if (n instanceof TableView<?> tv) {
            for (Object row : tv.getItems()) if (String.valueOf(row).contains(text)) return true;
        }
        if (n instanceof Parent p) for (Node c : p.getChildrenUnmodifiable()) if (contains(c, text)) return true;
        return false;
    }

    /** The dialog open now (the last window that is not the main one), waiting for it to appear. */
    protected DialogPane dialog() {
        until(() -> topDialog() != null, "a dialog");
        return get(this::topDialog);
    }

    private DialogPane topDialog() {
        DialogPane found = null;
        for (Window w : Window.getWindows())
            if (w != stage && w.isShowing() && w.getScene() != null && w.getScene().getRoot().lookup(".dialog-pane") instanceof DialogPane d) found = d;
        return found;
    }

    protected boolean dialogOpen() {
        return get(() -> topDialog() != null);
    }

    /** The button of the open dialog with this text. */
    protected Button button(String text) {
        DialogPane d = dialog();
        return get(() -> d.lookupAll(".button").stream().filter(n -> n instanceof Button b && text.equals(b.getText()))
                .map(n -> (Button) n).findFirst().orElseThrow(() -> new AssertionError("no button " + text)));
    }

    /** Presses a button of the open dialog and waits for the dialog to go. */
    protected void press(String text) {
        Button b = button(text);
        robot.clickOn(b);
        settle();
    }

    /** The text field of the open dialog with this prompt (the grey hint inside it). */
    protected TextInputControl field(String prompt) {
        DialogPane d = dialog();
        return get(() -> d.lookupAll(".text-input").stream().filter(n -> n instanceof TextInputControl t && prompt.equals(t.getPromptText()))
                .map(n -> (TextInputControl) n).findFirst().orElseThrow(() -> new AssertionError("no field " + prompt)));
    }

    /** Writes into a field as the user would: clicks it, selects what is there, types. */
    protected void type(TextInputControl f, String text) {
        robot.clickOn(f);
        on(f::selectAll);
        robot.write(text);
        settle();
    }

    /** Right click on the visible text {@code target}, then click on the menu item {@code item}. */
    protected void menu(String target, String item) {
        contextMenu(() -> text(target));
        robot.clickOn(menuItem(item));
        settle();
    }

    /**
     * Right click on a node. Monocle does not turn the secondary button into a context menu request, as the
     * real platforms do: after the click, the request is sent here, at the same point.
     */
    protected void contextMenu(Supplier<Node> target) {
        robot.rightClickOn(target.get());
        settle();
        // The click may have rebuilt the view (a host chosen): the node is looked for again.
        Node n = target.get();
        on(() -> {
            Bounds b = n.localToScreen(n.getBoundsInLocal());
            Bounds l = n.getBoundsInLocal();
            double x = l.getMinX() + l.getWidth() / 2, y = l.getMinY() + l.getHeight() / 2;
            Event.fireEvent(n, new ContextMenuEvent(ContextMenuEvent.CONTEXT_MENU_REQUESTED,
                    x, y, b.getMinX() + b.getWidth() / 2, b.getMinY() + b.getHeight() / 2, false, new PickResult(n, x, y)));
        });
        settle();
    }

    /**
     * Chooses an item through submenus ("Split down", "Production/US", "api-us-1") of the open menu. The
     * pointer is not moved through them: going diagonally to a submenu, a real mouse (or the robot) crosses
     * the other items and opens their submenus instead.
     */
    protected void choose(String... path) {
        until(() -> all(".context-menu").stream().anyMatch(Node::isVisible), "a menu");
        on(() -> {
            ContextMenu open = null;
            for (Window w : Window.getWindows()) if (w instanceof ContextMenu c && c.isShowing() && !(c.getOwnerWindow() instanceof ContextMenu)) open = c;
            if (open == null) throw new AssertionError("no menu open");
            List<MenuItem> items = open.getItems();
            MenuItem found = null;
            for (String step : path) {
                found = items.stream().filter(i -> step.equals(i.getText())).findFirst().orElseThrow(() -> new AssertionError("no menu item " + step));
                if (found instanceof Menu m) items = m.getItems();
            }
            if (found.isDisable()) throw new AssertionError("menu item disabled: " + path[path.length - 1]);
            open.hide();
            found.fire();
        });
        settle();
    }

    protected boolean menuItemDisabled(String... path) {
        until(() -> all(".context-menu").stream().anyMatch(Node::isVisible), "a menu");
        return get(() -> {
            for (Window w : Window.getWindows()) {
                if (!(w instanceof ContextMenu c) || !c.isShowing()) continue;
                List<MenuItem> items = c.getItems();
                MenuItem found = null;
                for (String step : path) {
                    found = items.stream().filter(i -> step.equals(i.getText())).findFirst().orElse(null);
                    if (found == null) break;
                    if (found instanceof Menu m) items = m.getItems();
                }
                if (found != null) {
                    c.hide();
                    return found.isDisable();
                }
            }
            throw new AssertionError("no menu item " + String.join(" > ", path));
        });
    }

    protected Node menuItem(String item) {
        until(() -> all(".context-menu").stream().anyMatch(Node::isVisible), "a menu");
        return robot.lookup(n -> n instanceof Labeled l && item.equals(l.getText()) && n.isVisible()
                && n.getScene() != null && n.getScene().getWindow() instanceof ContextMenu).query();
    }

    /** An icon button, found by its tooltip (it has no text). */
    protected ButtonBase byTooltip(String tooltip) {
        return (ButtonBase) robot.lookup(n -> n instanceof ButtonBase b && b.getTooltip() != null
                && tooltip.equals(b.getTooltip().getText()) && b.isVisible()).query();
    }

    protected List<Session> sessions() {
        return get(() -> new ArrayList<>(app.state.sessions));
    }

    /** What the user types into the terminal of {@code s}, as xterm.js hands it to Java. */
    protected void typeInto(Session s, String data) {
        on(() -> {
            try {
                Field f = Terminal.class.getDeclaredField("bridge");
                f.setAccessible(true);
                ((Terminal.Bridge) f.get(SessionViews.terminal(s))).input(data);
            } catch (ReflectiveOperationException e) {
                throw new AssertionError(e);
            }
        });
    }

    protected FakeTransport transport(Session s) {
        return (FakeTransport) s.transport;
    }

    protected Host host(String name) {
        return get(() -> app.state.hosts.stream().filter(h -> h.name.equals(name)).findFirst().orElseThrow());
    }

    protected boolean hasHost(String name) {
        return get(() -> app.state.hosts.stream().anyMatch(h -> h.name.equals(name)));
    }

    // ── The made-up world ────────────────────────────────────────────────

    static final class MemoryRepository implements HostRepository {
        private Data data;

        MemoryRepository(List<Host> hosts) {
            data = new Data(new ArrayList<>(), hosts);
        }

        @Override
        public Data load() {
            return new Data(new ArrayList<>(data.folders()), new ArrayList<>(data.hosts()));
        }

        @Override
        public void save(Data d) {
            data = new Data(new ArrayList<>(d.folders()), new ArrayList<>(d.hosts()));
        }
    }

    static final class MemoryVault implements Vault {
        final Map<String, String> secrets = new HashMap<>();
        boolean open = true;
        private final List<Runnable> onUnlock = new ArrayList<>(), onLock = new ArrayList<>();

        @Override public boolean exists() { return true; }
        @Override public void onUnlock(Runnable r) { onUnlock.add(r); }
        @Override public void onLock(Runnable r) { onLock.add(r); }
        @Override public boolean isOpen() { return open; }
        @Override public void create(char[] main) { open = true; }
        @Override public boolean open(char[] main) {
            if (!"vault-password".equals(new String(main))) return false;
            open = true;
            onUnlock.forEach(Runnable::run);
            return true;
        }
        @Override public void lock() { open = false; onLock.forEach(Runnable::run); }
        @Override public boolean changePassword(char[] current, char[] fresh) { return "vault-password".equals(new String(current)); }
        @Override public String password(String key) { return open ? secrets.get(key) : null; }
        @Override public void remember(String key, String value) { secrets.put(key, value); }
        @Override public void forget(String key) { secrets.remove(key); }
        @Override public Map<String, String> entries() { return open ? new HashMap<>(secrets) : Map.of(); }
        @Override public void replaceAll(Map<String, String> entries) { secrets.clear(); secrets.putAll(entries); }
    }

    /** Resources and updates invented per host, answered at once. */
    static final class FakeMetrics implements MetricsService {
        Updates updates = new Updates(14, 3, true, "linux-image-6.8.0-51-generic", null, 0);
        final List<String> refreshed = new ArrayList<>();

        static Resources resources() {
            long gib = 1024L * 1024;
            return new Resources(4, 0.25, 8 * gib, 3 * gib, List.of(new Disk("/", 80 * gib, 20 * gib, 60 * gib), new Disk("/srv", 200 * gib, 190 * gib, 10 * gib)), 3 * 86_400 + 4 * 3600);
        }

        @Override public void refresh(Host h, boolean force, Consumer<MetricsResult> outcome) {
            synchronized (refreshed) { refreshed.add(h.name); }
            outcome.accept(new MetricsResult(resources(), null, System.currentTimeMillis()));
        }
        @Override public void updates(Host h, boolean force, Consumer<Updates> outcome) { outcome.accept(updates); }
        @Override public boolean running(String hostId) { return false; }
        @Override public MetricsResult last(String hostId) { return null; }
        @Override public List<Double> cpuHistory(String hostId) { return List.of(0.1, 0.3, 0.2, 0.5); }
        @Override public void forget(String hostId) {}
        @Override public void stop() {}
    }

    static final class FakeProbe implements ProbeService {
        @Override public void launch(Consumer<Map<String, HostStatus>> outcomes) {}
        @Override public void list(List<Host> hosts) {}
        @Override public void every(int minutes) {}
        @Override public void all() {}
        @Override public void stop() {}
    }

    /** Connects at once, prints a prompt, keeps what it is sent. */
    static final class FakeTransport implements Transport {
        final Host host;
        final StringBuilder sent = new StringBuilder();
        volatile boolean closed;
        private Consumer<String> data = d -> {};
        private BiConsumer<SessionStatus, String> state = (s, m) -> {};

        FakeTransport(Host host) {
            this.host = host;
        }

        @Override public void onData(Consumer<String> cb) { data = cb; }
        @Override public void onState(BiConsumer<SessionStatus, String> cb) { state = cb; }
        @Override public void launch(int columns, int lines) {
            state.accept(SessionStatus.CONNECTED, null);
            data.accept(host.user + "@" + host.name + ":~$ ");
        }
        @Override public synchronized void send(String d) { sent.append(d); }
        @Override public void resize(int columns, int lines) {}
        @Override public void close() {
            if (closed) return;
            closed = true;
            state.accept(SessionStatus.CLOSED, null);
        }

        /** As if the server printed this. */
        void print(String text) {
            data.accept(text);
        }

        /** As if the server ended the shell (exit). */
        void end() {
            Platform.runLater(this::close);
        }

        synchronized String sent() {
            return sent.toString();
        }
    }
}
