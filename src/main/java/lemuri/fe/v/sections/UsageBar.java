package lemuri.fe.v.sections;

import java.time.Instant;
import java.time.LocalTime;
import java.time.ZoneId;
import javafx.geometry.Pos;
import javafx.scene.control.Label;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import lemuri.be.dto.Disk;
import lemuri.be.dto.MetricsResult;
import lemuri.be.util.I18n;

/** Usage bar with the percentage next to it; colours as in the rings: warning above 75%, critical above 90%. */
public final class UsageBar extends HBox {
    public UsageBar(double quota, String text, double width) {
        getStyleClass().add("usage-bar");
        double q = Math.min(1, Math.max(0, quota));
        Region value = new Region();
        value.getStyleClass().addAll("usage-bar-value", level(q));
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

    /** The style class of the bar for a quota between 0 and 1. */
    public static String level(double quota) {
        return quota >= 0.9 ? "critical" : quota >= 0.75 ? "warn" : "normal";
    }

    /** Every disk of a reading, one per line, with the time of the reading and the last failure if any. */
    public static String disksTip(MetricsResult e) {
        StringBuilder tip = new StringBuilder();
        for (Disk x : e.resources().disks())
            tip.append(String.format("%-18s %3d%%  %s%n", x.dot(), Math.round(x.quota() * 100), I18n.t("disk.freeOf", measure(x.freeKb()), measure(x.totalKb()))));
        tip.append(I18n.t("metrics.readAt", LocalTime.ofInstant(Instant.ofEpochMilli(e.when()), ZoneId.systemDefault()).withNano(0).toString()));
        if (e.error() != null) tip.append("\n").append(I18n.t("metrics.tip.failed", e.error()));
        return tip.toString();
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
