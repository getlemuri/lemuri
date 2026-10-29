package lemuri.fe.v;

import java.awt.image.BufferedImage;
import java.io.IOException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.List;
import java.util.Map;
import javafx.scene.SnapshotParameters;
import javafx.scene.canvas.Canvas;
import javafx.scene.canvas.GraphicsContext;
import javafx.scene.image.Image;
import javafx.scene.image.ImageView;
import javafx.scene.image.PixelReader;
import javafx.scene.image.PixelWriter;
import javafx.scene.image.WritableImage;
import javafx.scene.layout.Pane;
import javafx.scene.layout.Region;
import javafx.scene.paint.Color;
import javafx.scene.paint.CycleMethod;
import javafx.scene.paint.LinearGradient;
import javafx.scene.paint.RadialGradient;
import javafx.scene.paint.Stop;
import javafx.scene.shape.StrokeLineCap;
import javax.imageio.ImageIO;

/**
 * The program icon: the lemur peeking from behind the # (the picture in {@code logo.png}, with a
 * transparent background) on the dark blue water of the splash, inside a rounded square.
 * Below 32 px the lemur would be a grey smudge, so there the drawn # on a blue background remains.
 */
public final class AppIcon {
    /** The sizes given to the window: the system picks the most suitable one. */
    private static final int[] SIZES = {16, 24, 32, 48, 64, 128, 256};
    private static List<Image> images;
    private static Image logo;
    private static final Map<Integer, Image> scaledLogos = new HashMap<>();

    private AppIcon() {
    }

    /** The window icons, drawn only once (call on the JavaFX thread). */
    public static List<Image> images() {
        if (images == null) {
            images = new ArrayList<>();
            for (int m : SIZES) images.add(draw(m));
        }
        return images;
    }

    /** The lemur and the #, transparent around them, loaded once at full size (512 px). */
    static Image logo() {
        if (logo == null) logo = new Image(AppIcon.class.getResource("/lemuri/logo.png").toExternalForm());
        return logo;
    }

    /**
     * The logo already reduced to {@code side} pixels by the image loader, which filters well: a canvas or an
     * ImageView shrinking the 512 px picture by a lot in one step turns the fur into noise.
     */
    static Image logo(double side) {
        int px = Math.max(1, (int) Math.round(side));
        return scaledLogos.computeIfAbsent(px, k ->
                new Image(AppIcon.class.getResource("/lemuri/logo.png").toExternalForm(), k, k, true, true));
    }

    /**
     * The icon to put in the interface, shown at {@code side} pixels, without the square: just the lemur
     * and the #. The large picture is scaled down, so it stays sharp on high-density screens.
     */
    public static ImageView view(double side) {
        // Twice the size, for high-density screens.
        ImageView v = new ImageView(logo(side * 2));
        v.setFitWidth(side);
        v.setFitHeight(side);
        v.setSmooth(true);
        return v;
    }

    /**
     * The water filling its whole area, edge to edge, with the lemur in the middle: redrawn at every size
     * change (the box at the top left follows the height of the bar).
     */
    public static Region tile() {
        Canvas canvas = new Canvas();
        Pane p = new Pane(canvas) {
            @Override
            protected void layoutChildren() {
                double w = getWidth(), h = getHeight();
                if (w <= 0 || h <= 0 || (canvas.getWidth() == w && canvas.getHeight() == h)) return;
                canvas.setWidth(w);
                canvas.setHeight(h);
                GraphicsContext g = canvas.getGraphicsContext2D();
                g.clearRect(0, 0, w, h);
                g.drawImage(faded(w, h), 0, 0, w, h);
                double side = Math.min(w, h) * 0.92;
                g.drawImage(logo(side * 2), (w - side) / 2, (h - side) / 2, side, side);
            }
        };
        p.setMinSize(0, 0);
        return p;
    }

    /**
     * The full-area water with fading edges: solid in the centre, more and more transparent towards the sides
     * and the corners, until it disappears. This way it blends with the bar colour, whatever the theme.
     * Drawn at twice the size to stay sharp on high-density screens.
     */
    private static WritableImage faded(double w, double h) {
        int pw = (int) Math.ceil(w * 2), ph = (int) Math.ceil(h * 2);
        Canvas canvas = new Canvas(pw, ph);
        GraphicsContext g = canvas.getGraphicsContext2D();
        g.scale(2, 2);
        fill(g, w, h);
        SnapshotParameters sp = new SnapshotParameters();
        sp.setFill(Color.TRANSPARENT);
        WritableImage im = canvas.snapshot(sp, null);
        PixelReader pr = im.getPixelReader();
        PixelWriter pw2 = im.getPixelWriter();
        for (int y = 0; y < ph; y++) {
            double dy = (y + 0.5 - ph / 2.0) / (ph / 2.0);
            for (int x = 0; x < pw; x++) {
                double dx = (x + 0.5 - pw / 2.0) / (pw / 2.0);
                // "Elliptical" distance from the centre: 1 on the sides, more in the corners.
                double t = Math.min(1, Math.max(0, (Math.sqrt(dx * dx + dy * dy) - 0.45) / 0.55));
                double remains = 1 - t * t * (3 - 2 * t);
                int argb = pr.getArgb(x, y);
                int a = (int) Math.round((argb >>> 24) * remains);
                pw2.setArgb(x, y, (a << 24) | (argb & 0x00ffffff));
            }
        }
        return im;
    }

    /**
     * The icon for the tray, on a transparent background. The panel shows it at 16-24 pixels, so it is only the
     * lemur's head: ears, eyes and muzzle fill the square, with a small # in the bottom left corner.
     */
    public static WritableImage square(int side) {
        Canvas canvas = new Canvas(side, side);
        GraphicsContext g = canvas.getGraphicsContext2D();
        // The crop in the 512 px logo: x 250-505, y 45-300.
        double x0 = 250, y0 = 45, crop = 255, scale = side / crop;
        Image big = logo(512 * scale);
        g.drawImage(big, x0 * scale, y0 * scale, crop * scale, crop * scale, 0, 0, side, side);
        SnapshotParameters p = new SnapshotParameters();
        p.setFill(Color.TRANSPARENT);
        WritableImage im = canvas.snapshot(p, null);
        // The top of the # comes into the bottom left corner, under the paws (logo x < 345, y > 215): its white
        // and grey go, the dark paws stay.
        PixelReader pr = im.getPixelReader();
        PixelWriter pw = im.getPixelWriter();
        int right = (int) Math.min(side, Math.ceil((345 - x0) * scale)), top = (int) Math.max(0, Math.floor((215 - y0) * scale));
        for (int y = top; y < side; y++)
            for (int x = 0; x < right; x++) {
                Color c = pr.getColor(x, y);
                if (c.getBrightness() > 0.5 && c.getSaturation() < 0.18) pw.setArgb(x, y, 0);
            }
        // In that corner, now dark with the paws, a small # of the splash: the mark of the app next to the lemur.
        Canvas marked = new Canvas(side, side);
        GraphicsContext m = marked.getGraphicsContext2D();
        m.drawImage(im, 0, 0);
        double box = side * 0.46;
        m.save();
        m.translate(0, side - box);
        m.scale(box / 256, box / 256);
        hash(m);
        m.restore();
        return marked.snapshot(p, null);
    }

    /** The same square saved as PNG, for the AppIndicator icon that the system reads from a file. */
    public static void savePng(int side, Path file) throws IOException {
        ImageIO.write(forAwt(side), "png", file.toFile());
    }

    /** The dark water, lighter in the centre. */
    private static void fill(GraphicsContext g, double w, double h) {
        g.clearRect(0, 0, w, h);
        g.setFill(new RadialGradient(0, 0, w / 2, h / 2, Math.max(w, h) * 0.7, false, CycleMethod.NO_CYCLE,
                new Stop(0, Color.web("#165089")), new Stop(0.6, Color.web("#0a2c4d")), new Stop(1, Color.web("#061a2e"))));
        g.fillRect(0, 0, w, h);
    }

    /** The icon at one size, with a transparent background outside the rounded square. */
    public static WritableImage draw(int side) {
        Canvas canvas = new Canvas(side, side);
        GraphicsContext g = canvas.getGraphicsContext2D();
        double s = side / 256.0;
        g.scale(s, s);
        boolean small = side < 32;
        double margin = small ? 4 : 14, l = 256 - 2 * margin, arc = l * 0.44;

        // The background: the dark water of the splash, lighter in the centre.
        g.setFill(new RadialGradient(0, 0, 128, 118, 150, false, CycleMethod.NO_CYCLE,
                new Stop(0, Color.web("#165089")), new Stop(0.6, Color.web("#0a2c4d")), new Stop(1, Color.web("#061a2e"))));
        g.fillRoundRect(margin, margin, l, l, arc, arc);

        if (small) {
            hash(g);
        } else {
            // A soft light behind the lemur, so the black fur stands out from the dark water.
            g.setFill(new RadialGradient(0, 0, 128, 118, 100, false, CycleMethod.NO_CYCLE,
                    new Stop(0, Color.web("#4fb3ff", 0.35)), new Stop(1, Color.web("#4fb3ff", 0))));
            g.fillOval(28, 18, 200, 200);
            double logoSide = l * 0.86;
            g.drawImage(logo(logoSide * s), 128 - logoSide / 2, 128 - logoSide / 2, logoSide, logoSide);
            // A thread of light on the top edge, like a drop.
            g.setStroke(new LinearGradient(0, margin, 0, margin + l, false, CycleMethod.NO_CYCLE,
                    new Stop(0, Color.web("#7fd3ff", 0.55)), new Stop(0.5, Color.web("#7fd3ff", 0.05))));
            g.setLineWidth(3);
            g.strokeRoundRect(margin + 1.5, margin + 1.5, l - 3, l - 3, arc - 3, arc - 3);
        }

        SnapshotParameters p = new SnapshotParameters();
        p.setFill(Color.TRANSPARENT);
        return canvas.snapshot(p, null);
    }

    /** The hand-drawn # of the splash, thick and without a halo, for the sizes too small for the lemur. */
    private static void hash(GraphicsContext g) {
        g.save();
        g.translate(128, 128);
        g.scale(2.05, 2.05);
        g.setStroke(Color.WHITE);
        g.setLineCap(StrokeLineCap.ROUND);
        g.setLineWidth(15);
        double h = 46, w = 38, sp = 15;
        g.strokeLine(-sp + 6, -h, -sp - 6, h);
        g.strokeLine(sp + 6, -h, sp - 6, h);
        g.strokeLine(-w, -sp, w + 4, -sp);
        g.strokeLine(-w - 4, sp, w, sp);
        g.restore();
    }

    /** The icon in AWT format for the tray, without going through javafx-swing. */
    public static BufferedImage forAwt(int side) {
        WritableImage im = square(side);
        PixelReader pr = im.getPixelReader();
        int w = (int) im.getWidth(), h = (int) im.getHeight();
        var b = new BufferedImage(w, h, BufferedImage.TYPE_INT_ARGB);
        for (int y = 0; y < h; y++)
            for (int x = 0; x < w; x++) b.setRGB(x, y, pr.getArgb(x, y));
        return b;
    }
}
