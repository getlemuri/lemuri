package lemuri.fe.v.sections;

import javafx.geometry.Pos;
import javafx.scene.control.Label;
import javafx.scene.layout.Pane;
import javafx.scene.layout.StackPane;
import javafx.scene.layout.VBox;
import javafx.scene.shape.Arc;
import javafx.scene.shape.ArcType;
import javafx.scene.shape.Circle;

/**
 * Usage ring: percentage in the centre, detail below. The colour turns
 * to warning above 75% and to critical above 90%; for a score, where more is
 * better, to warning below 80% and to critical below 50%.
 */
public final class Gauge extends VBox {
    private static final double RADIUS = 38;
    private final Arc value = new Arc(0, 0, RADIUS, RADIUS, 90, 0);
    private final Label percent = new Label();
    private final Label detail = new Label();
    private final boolean score;

    public Gauge(String title) {
        this(title, false);
    }

    /** With {@code score} more is better: full and green when everything is right. */
    public Gauge(String title, boolean score) {
        this.score = score;
        getStyleClass().add("gauge");
        setAlignment(Pos.CENTER);
        setSpacing(4);
        // Absolute positions: a partial arc centred by a StackPane would shift.
        Circle background = new Circle(46, 46, RADIUS);
        background.getStyleClass().add("gauge-bg");
        value.setCenterX(46);
        value.setCenterY(46);
        value.setType(ArcType.OPEN);
        value.getStyleClass().add("gauge-value");
        percent.getStyleClass().add("gauge-text");
        Pane drawing = new Pane(background, value);
        StackPane ring = new StackPane(drawing, percent);
        ring.setMinSize(92, 92);
        ring.setPrefSize(92, 92);
        ring.setMaxSize(92, 92);
        Label t = new Label(title);
        t.getStyleClass().add("gauge-title");
        detail.getStyleClass().add("gauge-detail");
        getChildren().addAll(t, ring, detail);
    }

    /** {@code quota} 0-1, or null when the value is not available. */
    public void set(Double quota, String detailText) {
        double q = quota == null ? 0 : Math.min(1, Math.max(0, quota));
        // Negative length = clockwise starting from the top.
        value.setLength(-360 * q);
        value.getStyleClass().removeAll("gauge-normal", "gauge-good", "gauge-warn", "gauge-critical");
        if (score) value.getStyleClass().add(q < 0.5 ? "gauge-critical" : q < 0.8 ? "gauge-warn" : "gauge-good");
        else value.getStyleClass().add(q >= 0.9 ? "gauge-critical" : q >= 0.75 ? "gauge-warn" : "gauge-normal");
        percent.setText(quota == null ? "…" : Math.round(q * 100) + "%");
        detail.setText(detailText);
    }
}
