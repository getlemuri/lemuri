package lemuri.fe.v.sections;

import javafx.geometry.Pos;
import javafx.scene.control.Label;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;

/** Usage bar with the percentage next to it; colours as in the rings: warning above 75%, critical above 90%. */
public final class UsageBar extends HBox {
    public UsageBar(double quota, String text, double width) {
        getStyleClass().add("usage-bar");
        double q = Math.min(1, Math.max(0, quota));
        Region value = new Region();
        value.getStyleClass().addAll("usage-bar-value", q >= 0.9 ? "critical" : q >= 0.75 ? "warn" : "normal");
        StackPane trace = new StackPane(value);
        trace.getStyleClass().add("usage-bar-track");
        trace.setAlignment(Pos.CENTER_LEFT);
        trace.setPrefWidth(width);
        trace.setMinWidth(width);
        value.maxWidthProperty().bind(trace.widthProperty().multiply(q));
        value.setMinWidth(0);
        setAlignment(Pos.CENTER_LEFT);
        getChildren().addAll(trace, new Label(Math.round(q * 100) + "%  " + text));
    }

    /** Kibibytes in a readable unit, the way df -h does. */
    public static String measure(long kb) {
        double v = kb;
        String[] unit = {"KiB", "MiB", "GiB", "TiB"};
        int i = 0;
        while (v >= 1024 && i < unit.length - 1) {
            v /= 1024;
            i++;
        }
        return (v >= 100 || i == 0 ? String.valueOf(Math.round(v)) : String.format("%.1f", v)) + " " + unit[i];
    }
}
