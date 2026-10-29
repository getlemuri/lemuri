package lemuri.fe.v;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;
import java.util.logging.Logger;
import org.freedesktop.dbus.DBusPath;
import org.freedesktop.dbus.Struct;
import org.freedesktop.dbus.Tuple;
import org.freedesktop.dbus.annotations.DBusInterfaceName;
import org.freedesktop.dbus.annotations.Position;
import org.freedesktop.dbus.connections.impl.DBusConnection;
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder;
import org.freedesktop.dbus.exceptions.DBusException;
import org.freedesktop.dbus.interfaces.DBusInterface;
import org.freedesktop.dbus.interfaces.Properties;
import org.freedesktop.dbus.messages.DBusSignal;
import org.freedesktop.dbus.types.UInt32;
import org.freedesktop.dbus.types.Variant;
import lemuri.be.util.I18n;
import lemuri.be.util.Log;

/**
 * The top-right icon on Linux as a real AppIndicator (StatusNotifierItem over D-Bus),
 * the same protocol as the other Ubuntu icons: no clipped box as with the old AWT
 * XEmbed tray. The icon is an image looked up by name; the menu goes through dbusmenu.
 * A click opens the menu, a double click shows or minimizes the window.
 */
final class AppIndicator {
    private static final Logger LOG = Log.of(AppIndicator.class);
    private static final String PATH = "/StatusNotifierItem", MENU_PATH = "/MenuBar";
    /** The name of the icon in ~/.lemuri/icons: the PNG is written by the Tray, with the lemur on a transparent background. */
    static final String ICON = "lemuri-tray";
    private static final int SHOW = 1, QUIT = 2, GHOST_OFF = 4;

    private final DBusConnection bus;
    private final Menu menu;

    static final Path ICON_DIR = Path.of(System.getProperty("user.home"), ".lemuri", "icons");

    private AppIndicator(DBusConnection bus, Menu menu) {
        this.bus = bus;
        this.menu = menu;
    }

    /** Registers the icon; if there is no D-Bus or nobody collects the icons, it throws and AWT is used. */
    static AppIndicator launch(Runnable active, Runnable show, Runnable quit) throws DBusException, IOException {
        Path folder = ICON_DIR;
        // Icons of earlier versions: if they stay there, the system might still show them.
        for (String old : new String[] {"lemuri-panel.svg", "lemuri-vortice.png", "vpsman-tray.png"}) Files.deleteIfExists(folder.resolve(old));
        DBusConnection bus = DBusConnectionBuilder.forSessionBus().withShared(false).build();
        boolean registered = false;
        Menu menu = new Menu(show, quit);
        try {
            String name = "org.kde.StatusNotifierItem-" + ProcessHandle.current().pid() + "-1";
            bus.requestBusName(name);
            bus.exportObject(PATH, new MenuEntry(folder.toString(), active));
            bus.exportObject(MENU_PATH, menu);
            bus.getRemoteObject("org.kde.StatusNotifierWatcher", "/StatusNotifierWatcher", Watcher.class)
                    .RegisterStatusNotifierItem(name);
            registered = true;
        } finally {
            // Whatever failed (nobody collects the icons, a D-Bus error), the connection goes with it.
            if (!registered) bus.close();
        }
        return new AppIndicator(bus, menu);
    }

    /** "Exit ghost mode" is in the menu only while ghost mode is on: the panel is told to read the menu again. */
    void ghost(boolean on) {
        if (menu.ghost == on) return;
        menu.ghost = on;
        int revision = ++menu.revision;
        try {
            bus.sendMessage(new DbusMenu.LayoutUpdated(MENU_PATH, new UInt32(revision), 0));
        } catch (DBusException | RuntimeException e) {
            // RuntimeException: D-Bus reports a lost bus with errors of its own type; the menu is then only stale.
            LOG.fine("Tray menu not updated: " + e);
        }
    }

    void close() {
        try {
            bus.close();
        } catch (IOException ignored) {
            // closing anyway: the session bus drops the icon with the connection
        }
    }

    // ── D-Bus interfaces ─────────────────────────────────────────────────────

    @DBusInterfaceName("org.kde.StatusNotifierWatcher")
    public interface Watcher extends DBusInterface {
        void RegisterStatusNotifierItem(String service);
    }

    @DBusInterfaceName("org.kde.StatusNotifierItem")
    public interface StatusNotifierItem extends DBusInterface {
        void Activate(int x, int y);

        void SecondaryActivate(int x, int y);

        void ContextMenu(int x, int y);

        void Scroll(int delta, String orientation);
    }

    @DBusInterfaceName("com.canonical.dbusmenu")
    public interface DbusMenu extends DBusInterface {
        MenuLayout<UInt32, Item> GetLayout(int parent, int depth, List<String> properties);

        List<MenuProperty> GetGroupProperties(List<Integer> ids, List<String> properties);

        Variant<?> GetProperty(int id, String name);

        void Event(int id, String event, Variant<?> data, UInt32 when);

        List<Integer> EventGroup(List<MenuEvent> events);

        // AboutToShowGroup is missing: dbus-java cannot export its reply and the Ubuntu extension does not use it.
        boolean AboutToShow(int id);

        /** The menu changed: the panel asks for the layout again. */
        class LayoutUpdated extends DBusSignal {
            public final UInt32 revision;
            public final int parent;

            public LayoutUpdated(String path, UInt32 revision, int parent) throws DBusException {
                super(path, revision, parent);
                this.revision = revision;
                this.parent = parent;
            }
        }
    }

    /** A menu item: (ia{sv}av), with the children inside variants. */
    public static final class Item extends Struct {
        @Position(0)
        public final int id;
        @Position(1)
        public final Map<String, Variant<?>> properties;
        @Position(2)
        public final List<Variant<?>> childNodes;

        public Item(int id, Map<String, Variant<?>> properties, List<Variant<?>> childNodes) {
            this.id = id;
            this.properties = properties;
            this.childNodes = childNodes;
        }
    }

    /** GetLayout reply: (u(ia{sv}av)). dbus-java reads the types of multiple replies from the generic parameters. */
    public static final class MenuLayout<R, V> extends Tuple {
        @Position(0)
        public final R revision;
        @Position(1)
        public final V root;

        public MenuLayout(R revision, V root) {
            this.revision = revision;
            this.root = root;
        }
    }

    public static final class MenuProperty extends Struct {
        @Position(0)
        public final int id;
        @Position(1)
        public final Map<String, Variant<?>> properties;

        public MenuProperty(int id, Map<String, Variant<?>> properties) {
            this.id = id;
            this.properties = properties;
        }
    }

    public static final class MenuEvent extends Struct {
        @Position(0)
        public final int id;
        @Position(1)
        public final String event;
        @Position(2)
        public final Variant<?> data;
        @Position(3)
        public final UInt32 when;

        public MenuEvent(int id, String event, Variant<?> data, UInt32 when) {
            this.id = id;
            this.event = event;
            this.data = data;
            this.when = when;
        }
    }

    // ── Exported objects ─────────────────────────────────────────────────────

    /** The properties read by the extension: icon name, folder to look it up in, menu. */
    private static final class MenuEntry implements StatusNotifierItem, Properties {
        private final Map<String, Variant<?>> properties = new LinkedHashMap<>();
        private final Runnable active;

        MenuEntry(String iconDir, Runnable active) {
            this.active = active;
            properties.put("Category", new Variant<>("ApplicationStatus"));
            properties.put("Id", new Variant<>("lemuri"));
            properties.put("Title", new Variant<>("Lemuri"));
            properties.put("Status", new Variant<>("Active"));
            properties.put("IconName", new Variant<>(ICON));
            properties.put("IconThemePath", new Variant<>(iconDir));
            properties.put("Menu", new Variant<>(new DBusPath(MENU_PATH)));
            properties.put("ItemIsMenu", new Variant<>(false));
        }

        @Override
        public void Activate(int x, int y) {
            active.run();
        }

        @Override
        public void SecondaryActivate(int x, int y) {
            active.run();
        }

        @Override
        public void ContextMenu(int x, int y) {
        }

        @Override
        public void Scroll(int delta, String orientation) {
        }

        @Override
        @SuppressWarnings("unchecked")
        public <A> A Get(String iface, String name) {
            return (A) properties.get(name);
        }

        @Override
        public <A> void Set(String iface, String name, A value) {
        }

        @Override
        public Map<String, Variant<?>> GetAll(String iface) {
            return properties;
        }

        @Override
        public String getObjectPath() {
            return PATH;
        }
    }

    /** The menu: Show Lemuri, Exit ghost mode (only in ghost mode), a separator, Quit. */
    private static final class Menu implements DbusMenu, Properties {
        private final Runnable show, quit;
        volatile boolean ghost;
        volatile int revision = 1;
        private final Map<String, Variant<?>> properties = new LinkedHashMap<>();

        Menu(Runnable show, Runnable quit) {
            this.show = show;
            this.quit = quit;
            properties.put("Version", new Variant<>(new UInt32(3)));
            properties.put("TextDirection", new Variant<>("ltr"));
            properties.put("Status", new Variant<>("normal"));
            properties.put("IconThemePath", new Variant<>(new String[0]));
        }

        private static Map<String, Variant<?>> item(String label) {
            Map<String, Variant<?>> m = new LinkedHashMap<>();
            m.put("label", new Variant<>(label));
            m.put("enabled", new Variant<>(true));
            m.put("visible", new Variant<>(true));
            return m;
        }

        private Map<String, Variant<?>> propertiesOf(int id) {
            return switch (id) {
                case SHOW -> item(I18n.t("tray.show"));
                case GHOST_OFF -> {
                    Map<String, Variant<?>> m = item(I18n.t("tray.ghostOff"));
                    m.put("visible", new Variant<>(ghost));
                    yield m;
                }
                case 3 -> Map.of("type", new Variant<>("separator"));
                case QUIT -> item(I18n.t("tray.quit"));
                default -> Map.of("children-display", new Variant<>("submenu"));
            };
        }

        @Override
        public MenuLayout<UInt32, Item> GetLayout(int parent, int depth, List<String> requests) {
            List<Variant<?>> childNodes = new ArrayList<>();
            if (parent == 0) {
                for (int id : ghost ? new int[] {SHOW, GHOST_OFF, 3, QUIT} : new int[] {SHOW, 3, QUIT})
                    childNodes.add(new Variant<>(new Item(id, propertiesOf(id), List.of()), "(ia{sv}av)"));
            }
            return new MenuLayout<>(new UInt32(revision), new Item(parent, propertiesOf(parent), childNodes));
        }

        @Override
        public List<MenuProperty> GetGroupProperties(List<Integer> ids, List<String> requests) {
            List<MenuProperty> r = new ArrayList<>();
            for (int id : ids) r.add(new MenuProperty(id, propertiesOf(id)));
            return r;
        }

        @Override
        public Variant<?> GetProperty(int id, String name) {
            Variant<?> v = propertiesOf(id).get(name);
            return v != null ? v : new Variant<>("");
        }

        @Override
        public void Event(int id, String event, Variant<?> data, UInt32 when) {
            if (!"clicked".equals(event)) return;
            // Exit ghost mode shows the normal window, which ends it (see Tray.bringToFront).
            if (id == SHOW || id == GHOST_OFF) show.run();
            else if (id == QUIT) quit.run();
        }

        @Override
        public List<Integer> EventGroup(List<MenuEvent> events) {
            for (MenuEvent e : events) Event(e.id, e.event, e.data, e.when);
            return List.of();
        }

        @Override
        public boolean AboutToShow(int id) {
            return false;
        }

        @Override
        @SuppressWarnings("unchecked")
        public <A> A Get(String iface, String name) {
            return (A) properties.get(name);
        }

        @Override
        public <A> void Set(String iface, String name, A value) {
        }

        @Override
        public Map<String, Variant<?>> GetAll(String iface) {
            return properties;
        }

        @Override
        public String getObjectPath() {
            return MENU_PATH;
        }
    }
}
