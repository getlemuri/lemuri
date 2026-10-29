package lemuri.fe.v;

import java.util.Random;
import javafx.animation.AnimationTimer;
import javafx.geometry.VPos;
import javafx.scene.Scene;
import javafx.scene.canvas.Canvas;
import javafx.scene.canvas.GraphicsContext;
import javafx.scene.effect.DropShadow;
import javafx.scene.layout.StackPane;
import javafx.scene.paint.Color;
import javafx.scene.paint.CycleMethod;
import javafx.scene.paint.RadialGradient;
import javafx.scene.paint.Stop;
import javafx.scene.shape.StrokeLineCap;
import javafx.scene.text.Font;
import javafx.scene.text.FontWeight;
import javafx.scene.text.TextAlignment;
import javafx.stage.Screen;
import javafx.stage.Stage;
import javafx.stage.StageStyle;
import lemuri.be.util.I18n;

/**
 * Splash screen: a water vortex that sucks everything towards the centre, out
 * of which the logo (the lemur behind the #) unrolls, then the Lemuri caption.
 * It lasts about three seconds; a click skips it. Drawn on a Canvas.
 */
public final class Splash {
    private static final double SIDE = 440;
    private static final double DURATION = 3.0;
    private static final int DROPS = 140;
    /** The side of the logo once it has come out. */
    private static final double LOGO = 230;

    private final Stage stage = new Stage(StageStyle.TRANSPARENT);
    private final Canvas canvas = new Canvas(SIDE, SIDE);
    private final Runnable after;
    private final double[] corner = new double[DROPS], radius = new double[DROPS], speed = new double[DROPS];
    private AnimationTimer round;
    private long start;
    private boolean finished;

    private Splash(Runnable after) {
        this.after = after;
        Random random = new Random(7);
        for (int i = 0; i < DROPS; i++) {
            corner[i] = random.nextDouble() * Math.PI * 2;
            radius[i] = 40 + random.nextDouble() * 170;
            speed[i] = 0.6 + random.nextDouble() * 0.8;
        }
    }

    /** Shows the screen and, when finished (or skipped with a click), calls {@code after}. */
    public static void show(Runnable after) {
        new Splash(after).launch();
    }

    private void launch() {
        StackPane root = new StackPane(canvas);
        root.setStyle("-fx-background-color: transparent;");
        Scene scene = new Scene(root, SIDE, SIDE, Color.TRANSPARENT);
        stage.setScene(scene);
        stage.setTitle("Lemuri");
        stage.getIcons().setAll(AppIcon.images());
        stage.setAlwaysOnTop(true);
        var screen = Screen.getPrimary().getVisualBounds();
        stage.setX(screen.getMinX() + (screen.getWidth() - SIDE) / 2);
        stage.setY(screen.getMinY() + (screen.getHeight() - SIDE) / 2);
        root.setOnMouseClicked(e -> close());
        round = new AnimationTimer() {
            @Override
            public void handle(long now) {
                if (start == 0) start = now;
                double t = (now - start) / 1e9;
                draw(t);
                if (t >= DURATION) close();
            }
        };
        stage.show();
        round.start();
    }

    private void close() {
        if (finished) return;
        finished = true;
        round.stop();
        // First the main window, then this one goes: with no windows open JavaFX would close the app.
        after.run();
        stage.close();
    }

    // ── Drawing ──────────────────────────────────────────────────────────────

    private void draw(double t) {
        GraphicsContext g = canvas.getGraphicsContext2D();
        double c = SIDE / 2;
        g.clearRect(0, 0, SIDE, SIDE);
        // Everything fades in the last half second.
        double fadeOut = 1 - closed((t - (DURATION - 0.5)) / 0.5);
        g.setGlobalAlpha(fadeOut);

        // The black disc: solid only in the middle, then fading slowly into transparency, with no visible edge.
        g.setFill(new RadialGradient(0, 0, c, c, SIDE / 2, false, CycleMethod.NO_CYCLE,
                new Stop(0, Color.web("#161616")), new Stop(0.3, Color.web("#0a0a0a")),
                new Stop(0.5, Color.web("#000000", 0.95)), new Stop(0.65, Color.web("#000000", 0.8)),
                new Stop(0.78, Color.web("#000000", 0.5)), new Stop(0.88, Color.web("#000000", 0.22)),
                new Stop(0.95, Color.web("#000000", 0.07)), new Stop(1, Color.web("#000000", 0))));
        g.fillOval(0, 0, SIDE, SIDE);

        // The vortex spins harder and harder, then tightens and calms down while the hash comes out.
        double force = Math.min(1, t / 1.2);
        double shrink = 1 - 0.55 * smooth((t - 0.9) / 0.9);
        double calm = 1 - 0.75 * smooth((t - 1.5) / 0.8);
        double rotation = t * t * 2.2 + t * 1.5;
        arms(g, c, rotation, shrink, force * calm);
        drops(g, c, t, shrink, force * calm);
        waves(g, c, t);

        // The centre lights up when the vortex sucks in.
        double light = smooth((t - 0.6) / 0.6) * (1 - 0.6 * smooth((t - 1.8) / 0.6));
        g.setFill(new RadialGradient(0, 0, c, c, 90, false, CycleMethod.NO_CYCLE,
                new Stop(0, Color.web("#bfe6ff", 0.85 * light)), new Stop(1, Color.web("#4fb3ff", 0))));
        g.fillOval(c - 90, c - 90, 180, 180);

        hash(g, c, t);
        caption(g, c, t);
        g.setGlobalAlpha(1);
    }

    /** Logarithmic spiral arms, lighter towards the centre. */
    private void arms(GraphicsContext g, double c, double rotation, double shrink, double alpha) {
        g.setLineCap(StrokeLineCap.ROUND);
        int arms = 6;
        for (int b = 0; b < arms; b++) {
            double phase = rotation + b * Math.PI * 2 / arms;
            double px = c, py = c;
            for (int i = 1; i <= 60; i++) {
                double s = i / 60.0;
                double r = (14 + 196 * s * s) * shrink;
                double a = phase - s * 4.2;
                double x = c + Math.cos(a) * r, y = c + Math.sin(a) * r;
                g.setStroke(Color.web("#7fd3ff", alpha * 0.55 * (1 - s) + 0.04 * alpha));
                g.setLineWidth(1 + 5 * (1 - s));
                if (i > 1) g.strokeLine(px, py, x, y);
                px = x;
                py = y;
            }
        }
    }

    /** Drops that spin and get sucked towards the centre; once there, they start again from the edge. */
    private void drops(GraphicsContext g, double c, double t, double shrink, double alpha) {
        for (int i = 0; i < DROPS; i++) {
            double r = radius[i] - (t * 60 * speed[i]) % 190;
            if (r < 12) r += 190;
            double a = corner[i] + t * (2.5 + 260 / r) * speed[i];
            double rr = r * shrink;
            double x = c + Math.cos(a) * rr, y = c + Math.sin(a) * rr;
            double dim = 1 + 2.2 * (r / 210);
            g.setFill(Color.web("#bfe6ff", alpha * (0.25 + 0.55 * (1 - r / 210))));
            g.fillOval(x - dim / 2, y - dim / 2, dim, dim);
        }
    }

    /** Water rings widening from the centre when the hash comes out. */
    private void waves(GraphicsContext g, double c, double t) {
        for (int k = 0; k < 3; k++) {
            double s = (t - 1.1 - k * 0.25) / 1.1;
            if (s <= 0 || s >= 1) continue;
            double r = 40 + 170 * s;
            // Faster than they grow: out where the disc fades they would draw its edge again.
            g.setStroke(Color.web("#7fd3ff", 0.5 * (1 - s) * (1 - s)));
            g.setLineWidth(2 * (1 - s) + 0.5);
            g.strokeOval(c - r, c - r, r * 2, r * 2);
        }
    }

    /** The logo comes out of the vortex spinning backwards and grows with a small bounce. */
    private void hash(GraphicsContext g, double c, double t) {
        double s = (t - 1.0) / 0.8;
        if (s <= 0) return;
        double p = Math.min(1, s);
        double scale = bounce(p);
        double round = -540 * Math.pow(1 - p, 3);
        double alpha = Math.min(1, s * 2);
        g.save();
        g.translate(c, c - 40 * smooth((t - 1.6) / 0.6));
        g.rotate(round);
        g.scale(scale, scale);
        g.setGlobalAlpha(g.getGlobalAlpha() * alpha);
        g.setEffect(new DropShadow(18, Color.web("#4fb3ff")));
        g.drawImage(AppIcon.logo(), -LOGO / 2, -LOGO / 2, LOGO, LOGO);
        g.setEffect(null);
        g.restore();
    }

    /** The caption rises and appears below the logo. */
    private void caption(GraphicsContext g, double c, double t) {
        double s = smooth((t - 1.6) / 0.6);
        if (s <= 0) return;
        g.save();
        g.setGlobalAlpha(g.getGlobalAlpha() * s);
        g.setFill(Color.WHITE);
        g.setFont(Font.font("Helvetica Neue", FontWeight.BOLD, 34));
        g.setTextAlign(TextAlignment.CENTER);
        g.setTextBaseline(VPos.CENTER);
        g.setEffect(new DropShadow(10, Color.BLACK));
        g.fillText("Lemuri", c, c + 112 + 16 * (1 - s));
        g.setEffect(null);
        g.setFont(Font.font("Helvetica Neue", 12));
        g.setFill(Color.web("#9fd2f5", s));
        g.fillText(I18n.t("app.tagline"), c, c + 140 + 16 * (1 - s));
        g.restore();
    }

    private static double closed(double x) {
        return Math.max(0, Math.min(1, x));
    }

    /** Smooth curve from 0 to 1 (smoothstep). */
    private static double smooth(double x) {
        x = closed(x);
        return x * x * (3 - 2 * x);
    }

    /** From 0 to 1, overshooting a little and coming back. */
    private static double bounce(double x) {
        double k = 1.70158;
        x = x - 1;
        return 1 + x * x * ((k + 1) * x + k);
    }
}
