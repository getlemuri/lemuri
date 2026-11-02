package lemuri.fe.v;

import java.io.IOException;
import java.util.Arrays;
import java.util.function.DoubleUnaryOperator;
import javafx.animation.AnimationTimer;
import javafx.stage.Stage;
import javafx.stage.Window;
import lemuri.be.util.X11;
import lemuri.fe.v.terminal.Terminal;

/**
 * Moves or resizes a window with the mouse, one step per frame, until the left button goes up. The pointer is read
 * from X11 on the whole screen: the drag events carry a position worked out from where JavaFX thinks the window
 * is, which lags behind while the window moves, so a window that follows them jumps back and forth. Here each
 * frame sends one request with position and size together, and only when they change: set one after the other,
 * the edge that should stay still would move first and come back after. Without X (or on a Mac) {@link #start}
 * and {@link #follow} give null and the caller keeps using the drag events.
 */
final class PointerMove {
    /** Where the window goes with the pointer at {@code x}, {@code y} (JavaFX screen coordinates). */
    interface Target {
        /** {x, y} to move the window, {x, y, width, height} to resize it too. */
        double[] at(double x, double y);
    }

    private final Window window;
    private final X11 x11;
    private final int id;
    private final Target target;
    private final boolean resizing;
    private final Runnable onEnd;
    private int[] last;
    private boolean over;

    private final AnimationTimer frames = new AnimationTimer() {
        @Override
        public void handle(long now) {
            step();
        }
    };

    private PointerMove(Window window, X11 x11, int id, Target target, boolean resizing, Runnable onEnd) {
        this.window = window;
        this.x11 = x11;
        this.id = id;
        this.target = target;
        this.resizing = resizing;
        this.onEnd = onEnd;
    }

    /**
     * Starts moving {@code window}: it was at {@code windowX}, {@code windowY} with the pointer at {@code pointerX},
     * {@code pointerY} (screen coordinates of JavaFX). {@code clampY}, if given, turns the wanted y into the y to
     * use. {@code onEnd} runs when the button goes up. Null if X11 cannot do it.
     */
    static PointerMove start(Window window, double pointerX, double pointerY, double windowX, double windowY,
                             DoubleUnaryOperator clampY, Runnable onEnd) {
        return begin(window, (x, y) -> {
            double top = windowY + y - pointerY;
            return new double[] {windowX + x - pointerX, clampY != null ? clampY.applyAsDouble(top) : top};
        }, false, onEnd);
    }

    /**
     * Starts resizing {@code window} to what {@code target} gives at each frame. Meanwhile the terminals keep
     * their columns and rows, worked out again once when the button goes up: doing it at every frame is what
     * makes a resize stutter. {@code onEnd} runs when the button goes up. Null if X11 cannot do it.
     */
    static PointerMove follow(Window window, Target target, Runnable onEnd) {
        return begin(window, target, true, onEnd);
    }

    private static PointerMove begin(Window window, Target target, boolean resizing, Runnable onEnd) {
        if (!(window instanceof Stage s) || window.getScene() == null) return null;
        X11 x = X11.pointerTracker();
        if (x == null) return null;
        int id = x.window(s.getTitle());
        int[] p = id != 0 ? x.pointer() : null;
        if (p == null || p[2] == 0) {
            close(x);
            return null;
        }
        PointerMove m = new PointerMove(window, x, id, target, resizing, onEnd);
        if (resizing) Terminal.holdSize(true);
        m.frames.start();
        return m;
    }

    /** Ends now (the button went up in JavaFX before X said so). */
    void stop() {
        if (over) return;
        over = true;
        frames.stop();
        close(x11);
        if (resizing) Terminal.holdSize(false);
        if (onEnd != null) onEnd.run();
    }

    private void step() {
        int[] p = x11.pointer();
        if (p == null || p[2] == 0) {
            stop();
            return;
        }
        double k = window.getOutputScaleX();
        double[] r = target.at(p[0] / k, p[1] / k);
        int[] px = new int[r.length];
        for (int i = 0; i < r.length; i++) px[i] = (int) Math.round(r[i] * k);
        if (Arrays.equals(px, last)) return;
        last = px;
        boolean sent = px.length == 4 ? x11.moveResize(id, px[0], px[1], px[2], px[3]) : x11.move(id, px[0], px[1]);
        if (!sent) stop();
    }

    private static void close(X11 x) {
        try {
            x.close();
        } catch (IOException e) {
            // already closed: nothing to release
        }
    }
}
