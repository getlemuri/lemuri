package lemuri.fe.v;

import java.util.List;
import javafx.application.Platform;
import javafx.geometry.Rectangle2D;
import javafx.stage.Screen;
import javafx.stage.Stage;
import lemuri.be.util.X11;

/**
 * Putting a window exactly where it should be, and bringing it to the front, on desktops that do not
 * always obey: GNOME on Wayland (through XWayland) places windows again on its own and refuses focus
 * requests that do not come from a click in the app. X11 is used to check and to ask the way a dock does.
 */
final class WindowPlacement {
    private final Stage window;
    /** Counts the placements: a check still running for an older one stops. */
    private int placing;

    WindowPlacement(Stage window) {
        this.window = window;
    }

    /**
     * Moves and resizes the window. By hand, as for the main window: setMaximized on a transparent stage would
     * also cover the system panels.
     */
    void place(Rectangle2D r) {
        window.setWidth(r.getWidth());
        window.setHeight(r.getHeight());
        // The window manager moves it only after the resize has gone through.
        Platform.runLater(() -> {
            window.setX(r.getMinX());
            window.setY(r.getMinY());
        });
        check(r, ++placing);
    }

    /**
     * GNOME may place the window again on its own a moment later (it does when the window appears over a maximized
     * one), and JavaFX does not always hear of it: it would still believe the window is where it was put, and
     * moving it there again would do nothing. So the real position is read from X11 for a second or so and, if it
     * is not the right one, the window is moved again.
     */
    private void check(Rectangle2D r, int which) {
        String title = window.getTitle();
        Thread t = new Thread(() -> {
            for (int i = 0; i < 6; i++) {
                try {
                    Thread.sleep(250);
                } catch (InterruptedException e) {
                    return;
                }
                int[] real = X11.bounds(title);
                if (real == null) return;
                if (Math.abs(real[0] - r.getMinX()) <= 2 && Math.abs(real[1] - r.getMinY()) <= 2
                        && Math.abs(real[2] - r.getWidth()) <= 2 && Math.abs(real[3] - r.getHeight()) <= 2) continue;
                Platform.runLater(() -> {
                    if (which != placing || !window.isShowing()) return;
                    // Off by one and back: setting the value JavaFX already has would not move anything.
                    window.setWidth(r.getWidth() + 1);
                    window.setWidth(r.getWidth());
                    window.setHeight(r.getHeight() + 1);
                    window.setHeight(r.getHeight());
                    window.setX(r.getMinX() + 1);
                    window.setX(r.getMinX());
                    window.setY(r.getMinY() + 1);
                    window.setY(r.getMinY());
                });
            }
        }, "window-place");
        t.setDaemon(true);
        t.start();
    }

    static Rectangle2D bounds(Stage s) {
        return new Rectangle2D(s.getX(), s.getY(), s.getWidth(), s.getHeight());
    }

    /** The usable area (without the system panels) of the screen the rectangle is on. */
    static Rectangle2D screen(Rectangle2D r) {
        var screens = Screen.getScreensForRectangle(r);
        return usable(screens.isEmpty() ? Screen.getPrimary() : screens.get(0));
    }

    /**
     * The usable area of a screen. JavaFX's visual bounds are wrong with more monitors (see X11.workAreas): a
     * window maximized on the laptop next to an external monitor went under the Ubuntu dock. GNOME's area for
     * that monitor is used when there is one; otherwise, as on the Mac, JavaFX's.
     */
    static Rectangle2D usable(Screen screen) {
        Rectangle2D all = screen.getBounds();
        double k = screen.getOutputScaleX();
        for (int[] a : workAreas()) {
            Rectangle2D w = new Rectangle2D(a[0] / k, a[1] / k, a[2] / k, a[3] / k);
            if (!all.contains(w.getMinX() + w.getWidth() / 2, w.getMinY() + w.getHeight() / 2)) continue;
            double x0 = Math.max(all.getMinX(), w.getMinX()), y0 = Math.max(all.getMinY(), w.getMinY());
            double x1 = Math.min(all.getMaxX(), w.getMaxX()), y1 = Math.min(all.getMaxY(), w.getMaxY());
            if (x1 > x0 && y1 > y0) return new Rectangle2D(x0, y0, x1 - x0, y1 - y0);
        }
        return screen.getVisualBounds();
    }

    private static List<int[]> areas = List.of();
    private static long areasRead;

    /** X11.workAreas, read again at most once a second: dragging the window asks for it at every move (JavaFX thread). */
    private static List<int[]> workAreas() {
        long now = System.currentTimeMillis();
        if (now - areasRead > 1000) {
            areas = X11.workAreas();
            areasRead = now;
        }
        return areas;
    }

    /**
     * Makes sure the window really comes to the front with the keyboard. When the request does not come from a
     * click in the app (F11 from another app, the tray icon) GNOME refuses toFront/requestFocus and shows a
     * "ready" notification instead: then the window is activated the way a dock does it. Tried a few times,
     * because the window manager maps the window a moment after show().
     */
    static void activate(Stage s) {
        String title = s.getTitle();
        Thread t = new Thread(() -> {
            for (int i = 0; i < 10; i++) {
                if (X11.isActive(title)) return;
                X11.activate(title);
                try {
                    Thread.sleep(100);
                } catch (InterruptedException e) {
                    return;
                }
            }
        }, "activate");
        t.setDaemon(true);
        t.start();
    }
}
