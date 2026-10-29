package lemuri.fe.v;

import java.awt.AWTException;
import java.awt.EventQueue;
import java.awt.MenuItem;
import java.awt.PopupMenu;
import java.awt.SystemTray;
import java.awt.TrayIcon;
import java.awt.event.MouseAdapter;
import java.awt.event.MouseEvent;
import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.file.Files;
import java.util.logging.Logger;
import javafx.application.Platform;
import javafx.stage.Stage;
import org.freedesktop.dbus.exceptions.DBusException;
import lemuri.be.util.I18n;
import lemuri.be.util.Log;
import lemuri.be.util.Os;

/**
 * The icon in the notification area (top right on Ubuntu). It first tries the AppIndicator
 * via D-Bus ({@link AppIndicator}), which Ubuntu draws like its own icons; if that is missing, it falls
 * back to the AWT SystemTray, which there ends up clipped in a box. The menu has Show, Exit ghost mode and
 * Quit; every way of showing the window from here also ends ghost mode ({@link GhostMode}), except a click
 * on the icon while the ghost is minimized: that brings the ghost back.
 * Both live on their own threads: towards JavaFX everything goes through runLater.
 */
public final class Tray {
    private static final Logger LOG = Log.of(Tray.class);
    private static volatile TrayIcon icon;
    private static volatile AppIndicator indicator;
    /** The main window's ghost mode: the icon brings it back or ends it. One per app, like the icon. */
    private static volatile GhostMode ghost;
    /** "Exit ghost mode" in the AWT menu, there only while ghost mode is on. */
    private static volatile MenuItem ghostOff;

    private Tray() {
    }

    /** Installs the icon, if the system has a tray; {@code quit} really closes the app (the only way, when the icon is there). */
    public static void install(Stage stage, GhostMode ghostMode, Runnable onQuit) {
        ghost = ghostMode;
        // On Mac, AWT and JavaFX together cause trouble with the menu bar: no tray there.
        if (Os.MAC) return;
        // The images are drawn here, on the JavaFX thread; AWT then picks the right size.
        int[] sizes = {16, 22, 24, 32, 48};
        var images = new BufferedImage[sizes.length];
        for (int i = 0; i < sizes.length; i++) images[i] = AppIcon.forAwt(sizes[i]);
        // The AppIndicator icon, which the system reads from a file: the same lemur, at a large size (it scales it down).
        try {
            Files.createDirectories(AppIndicator.ICON_DIR);
            AppIcon.savePng(64, AppIndicator.ICON_DIR.resolve(AppIndicator.ICON + ".png"));
        } catch (IOException e) {
            LOG.warning("Cannot write the tray icon: " + e.getMessage());
        }
        new Thread(() -> {
            try {
                indicator = AppIndicator.launch(() -> Platform.runLater(() -> toggle(stage)),
                        () -> Platform.runLater(() -> bringToFront(stage)), () -> Platform.runLater(onQuit));
                return;
            } catch (DBusException | IOException | RuntimeException | LinkageError e) {
                // RuntimeException: D-Bus replies with errors of its own type (DBusExecutionException); LinkageError: no native library.
                LOG.info("AppIndicator not available, using the AWT tray: " + e);
            }
            try {
                // The first question to AWT also stays off the JavaFX thread: it starts the whole toolkit.
                if (!SystemTray.isSupported()) return;
                SystemTray tray = SystemTray.getSystemTray();
                int side = tray.getTrayIconSize().width, choice = sizes.length - 1;
                for (int i = sizes.length - 1; i >= 0; i--) if (sizes[i] >= side) choice = i;
                var image = images[choice];
                PopupMenu menu = new PopupMenu();
                MenuItem show = new MenuItem(I18n.t("tray.show"));
                show.addActionListener(e -> Platform.runLater(() -> bringToFront(stage)));
                MenuItem normal = new MenuItem(I18n.t("tray.ghostOff"));
                normal.addActionListener(e -> Platform.runLater(() -> bringToFront(stage)));
                MenuItem quit = new MenuItem(I18n.t("tray.quit"));
                quit.addActionListener(e -> Platform.runLater(onQuit));
                menu.add(show);
                menu.addSeparator();
                menu.add(quit);
                TrayIcon ti = new TrayIcon(image, "Lemuri", menu);
                ti.setImageAutoSize(true);
                ti.addMouseListener(new MouseAdapter() {
                    @Override
                    public void mouseClicked(MouseEvent e) {
                        if (e.getButton() == MouseEvent.BUTTON1) Platform.runLater(() -> toggle(stage));
                    }
                });
                tray.add(ti);
                ghostOff = normal;
                icon = ti;
            } catch (AWTException | RuntimeException | LinkageError e) {
                // RuntimeException: headless or unsupported tray (HeadlessException, UnsupportedOperationException).
                LOG.warning("No system tray: " + e);
            }
        }, "tray").start();
    }

    /** Ghost mode began or ended: "Exit ghost mode" appears in the icon menu only while it is on. */
    static void ghostMode(boolean on) {
        AppIndicator a = indicator;
        if (a != null) {
            new Thread(() -> a.ghost(on), "tray-menu").start();
            return;
        }
        TrayIcon ti = icon;
        MenuItem item = ghostOff;
        if (ti == null || item == null) return;
        EventQueue.invokeLater(() -> {
            PopupMenu menu = ti.getPopupMenu();
            menu.remove(item);
            if (on) menu.insert(item, 1);
        });
    }

    /** True if the icon is there: then closing the window only hides it, and you quit from the icon menu. */
    public static boolean present() {
        return indicator != null || icon != null;
    }

    /** Hides the window leaving the app alive in the tray. */
    public static void hide(Stage stage) {
        // Without this, JavaFX would close the app when no window is left visible.
        Platform.setImplicitExit(false);
        stage.hide();
    }

    /** Removes the icon: without this, the D-Bus or AWT threads would keep the program alive after closing. */
    public static void remove() {
        AppIndicator addr = indicator;
        indicator = null;
        if (addr != null) addr.close();
        TrayIcon ti = icon;
        icon = null;
        if (ti != null) {
            try {
                SystemTray.getSystemTray().remove(ti);
            } catch (RuntimeException ignored) {
                // the tray is already gone: nothing to remove
            }
        }
    }

    private static void toggle(Stage stage) {
        if (ghost != null && ghost.active()) {
            // A minimized ghost comes back as it was; a ghost in view gives way to the normal window.
            if (!ghost.restore()) bringToFront(stage);
            return;
        }
        if (stage.isShowing() && !stage.isIconified() && stage.isFocused()) stage.setIconified(true);
        else bringToFront(stage);
    }

    /** Brings the window back into view: from the tray, or when the app is started again while already open. */
    public static void bringToFront(Stage stage) {
        // From ghost mode the way back is always the normal window.
        if (ghost != null) ghost.leave();
        if (!stage.isShowing()) stage.show();
        stage.setIconified(false);
        stage.toFront();
        stage.requestFocus();
        // The click was on the panel, not in the app: GNOME would only show a "ready" notification.
        WindowPlacement.activate(stage);
    }
}
