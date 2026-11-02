package lemuri.fe.v;

import java.util.Map;
import java.util.WeakHashMap;
import javafx.animation.Interpolator;
import javafx.animation.KeyFrame;
import javafx.animation.KeyValue;
import javafx.animation.Timeline;
import javafx.beans.property.DoubleProperty;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import javafx.stage.Window;
import javafx.util.Duration;

/**
 * A short fade when a window is shown or hidden with F11, instead of popping in or out. A window being shown stays
 * invisible for a moment first, so it is already in its place when it appears: placed after show(), it would
 * be seen for a frame where it was before. A hidden window always gets its full opacity back, so the other ways
 * of showing it (the tray, leaving ghost mode) never bring back an invisible window.
 * On Linux a TRANSPARENT stage ignores its own opacity (GTK leaves it to drawing that JavaFX does not use), so
 * there the content fades: the pixels get the alpha and the compositor blends them.
 */
final class WindowFade {
    private static final Duration IN = Duration.millis(140), OUT = Duration.millis(110);
    /** Time for the window manager to put a window that was just shown in its place. */
    private static final Duration SETTLE = Duration.millis(40);
    private static final Map<Window, Timeline> RUNNING = new WeakHashMap<>();
    /** The windows on their way out: still showing, but for F11 already hidden. */
    private static final Map<Window, Boolean> LEAVING = new WeakHashMap<>();

    private WindowFade() {
    }

    /** Call before show(): the window comes up invisible, then {@link #in} makes it appear. */
    static void hidden(Window w) {
        stop(w);
        opacity(w).set(0);
    }

    /** From the current opacity to full, after the window manager has had time to place the window. */
    static void in(Window w) {
        play(w, 1, IN, SETTLE, null);
    }

    /** True while the window is fading out: a second F11 brings it back instead of hiding it again. */
    static boolean leaving(Window w) {
        return LEAVING.containsKey(w);
    }

    /** To nothing, then {@code hide} runs and the window gets its opacity back for the next time it is shown. */
    static void out(Window w, Runnable hide) {
        play(w, 0, OUT, Duration.ZERO, () -> {
            hide.run();
            opacity(w).set(1);
        });
        LEAVING.put(w, true);
    }

    private static void play(Window w, double to, Duration time, Duration delay, Runnable then) {
        stop(w);
        Timeline t = new Timeline(new KeyFrame(time, new KeyValue(opacity(w), to, Interpolator.EASE_BOTH)));
        t.setDelay(delay);
        t.setOnFinished(e -> {
            RUNNING.remove(w);
            LEAVING.remove(w);
            if (then != null) then.run();
        });
        RUNNING.put(w, t);
        t.play();
    }

    /** What fades: the window itself, or its content when the window is transparent. */
    private static DoubleProperty opacity(Window w) {
        if (w instanceof Stage s && s.getStyle() == StageStyle.TRANSPARENT && s.getScene() != null && s.getScene().getRoot() != null)
            return s.getScene().getRoot().opacityProperty();
        return w.opacityProperty();
    }

    /** A fade still running is cut short: a quick second F11 reverses it from where it is. */
    private static void stop(Window w) {
        Timeline t = RUNNING.remove(w);
        LEAVING.remove(w);
        if (t != null) t.stop();
    }
}
