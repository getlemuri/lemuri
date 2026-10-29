package lemuri.fe.v;

import java.io.File;
import java.io.IOException;
import java.net.URI;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Collection;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import java.util.UUID;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.function.Consumer;
import java.util.logging.Logger;
import javafx.application.Platform;
import org.freedesktop.dbus.DBusPath;
import org.freedesktop.dbus.annotations.DBusInterfaceName;
import org.freedesktop.dbus.connections.impl.DBusConnection;
import org.freedesktop.dbus.connections.impl.DBusConnectionBuilder;
import org.freedesktop.dbus.exceptions.DBusException;
import org.freedesktop.dbus.interfaces.DBus;
import org.freedesktop.dbus.interfaces.DBusInterface;
import org.freedesktop.dbus.interfaces.DBusSigHandler;
import org.freedesktop.dbus.messages.DBusSignal;
import org.freedesktop.dbus.types.UInt32;
import org.freedesktop.dbus.types.Variant;
import lemuri.be.util.Log;
import lemuri.be.util.Os;
import lemuri.be.util.X11;

/**
 * Picks files with the desktop's own file chooser through the XDG portal (D-Bus), as sandboxed apps do: on
 * GNOME it is the Nautilus dialog, run by xdg-desktop-portal in its own process. The GTK chooser of JavaFX runs
 * inside this process, next to AWT (used for the primary selection), and there it could freeze. Without a
 * portal (Mac, other desktops) the caller falls back to the JavaFX chooser.
 */
final class PortalFiles {
    private static final String PORTAL = "org.freedesktop.portal.Desktop";
    private static final int WAIT_HOURS = 1;
    private static final Logger LOG = Log.of(PortalFiles.class);

    private PortalFiles() {}

    @DBusInterfaceName("org.freedesktop.portal.FileChooser")
    public interface FileChooser extends DBusInterface {
        DBusPath OpenFile(String parentWindow, String title, Map<String, Variant<?>> options);
    }

    @DBusInterfaceName("org.freedesktop.portal.Request")
    public interface Request extends DBusInterface {
        /** The dialog's reply: 0 chosen, 1 cancelled, 2 failed; the files in "uris". */
        class Response extends DBusSignal {
            final UInt32 response;
            final Map<String, Variant<?>> results;

            public Response(String path, UInt32 response, Map<String, Variant<?>> results) throws DBusException {
                super(path, response, results);
                this.response = response;
                this.results = Map.copyOf(results);
            }
        }
    }

    /**
     * Opens the portal dialog and returns at once. {@code chosen} gets, on the JavaFX thread, the files (empty if
     * the user cancels). Returns false, without calling it, if there is no portal: then the caller asks otherwise.
     */
    static boolean openFiles(String title, boolean multiple, Consumer<List<File>> chosen) {
        return open(title, Map.of("multiple", new Variant<>(multiple)), chosen);
    }

    /** Like {@link #openFiles}, for one folder: {@code chosen} gets it, or null if the user cancels. */
    static boolean openFolder(String title, Consumer<File> chosen) {
        return open(title, Map.of("directory", new Variant<>(true)), files -> chosen.accept(files.isEmpty() ? null : files.get(0)));
    }

    private static boolean open(String title, Map<String, Variant<?>> extra, Consumer<List<File>> chosen) {
        if (Os.MAC || System.getenv("DBUS_SESSION_BUS_ADDRESS") == null) return false;
        // The dialog stays above the main window: the portal wants its X id ("x11:<hex>").
        int id = X11.windowId("Lemuri");
        String parent = id != 0 ? "x11:" + Integer.toHexString(id) : "";
        DBusConnection bus;
        try {
            bus = DBusConnectionBuilder.forSessionBus().withShared(false).build();
        } catch (DBusException e) {
            LOG.fine("No session bus for the file portal: " + e);
            return false;
        }
        CompletableFuture<List<File>> answer = new CompletableFuture<>();
        try {
            // The answer comes as a signal on a path made of our bus name and a token: listen before asking.
            String token = "lemuri" + UUID.randomUUID().toString().replace("-", "");
            String expected = "/org/freedesktop/portal/desktop/request/" + bus.getUniqueName().substring(1).replace('.', '_') + "/" + token;
            String[] handle = {expected};
            DBusSigHandler<Request.Response> listener = r -> {
                if (!r.getPath().equals(handle[0]) && !r.getPath().equals(expected)) return;
                answer.complete(r.response.intValue() == 0 ? files(r.results.get("uris")) : List.of());
            };
            bus.addSigHandler(Request.Response.class, listener);
            // If the portal goes away (crashed, restarted) the answer never comes: as a cancel, and the bus is closed.
            bus.addSigHandler(DBus.NameOwnerChanged.class, n -> {
                if (PORTAL.equals(n.name) && n.newOwner.isEmpty()) answer.complete(List.of());
            });
            FileChooser portal = bus.getRemoteObject(PORTAL, "/org/freedesktop/portal/desktop", FileChooser.class);
            Map<String, Variant<?>> options = new HashMap<>(extra);
            options.put("handle_token", new Variant<>(token));
            options.put("modal", new Variant<>(true));
            handle[0] = portal.OpenFile(parent, title, options).getPath();
        } catch (DBusException | RuntimeException e) {
            LOG.info("File portal not available, using the JavaFX chooser: " + e);
            close(bus);
            return false;
        }
        // Last resort if no signal ever comes: nobody keeps a file dialog open for an hour.
        answer.orTimeout(WAIT_HOURS, TimeUnit.HOURS).whenComplete((list, e) -> {
            Platform.runLater(() -> chosen.accept(list != null ? list : List.of()));
            // This runs on the bus's own signal thread: closing from here waits on itself, so another thread does it.
            Thread.ofVirtual().name("portal-close").start(() -> close(bus));
        });
        return true;
    }

    /** The local files among the URIs of the answer; anything else (a remote location) is left out. */
    static List<File> files(Variant<?> uris) {
        List<File> out = new ArrayList<>();
        if (uris == null) return out;
        Object v = uris.getValue();
        Iterable<?> all = v instanceof Object[] a ? List.of(a) : v instanceof Collection<?> c ? c : List.of();
        for (Object u : all) {
            try {
                URI uri = URI.create(String.valueOf(u));
                if ("file".equals(uri.getScheme())) out.add(Path.of(uri).toFile());
            } catch (IllegalArgumentException e) {
                LOG.fine("Not a local file from the portal: " + u);
            }
        }
        return out;
    }

    private static void close(DBusConnection bus) {
        try {
            bus.close();
        } catch (IOException e) {
            LOG.fine("Closing the portal connection: " + e);
        }
    }
}
