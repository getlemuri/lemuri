package lemuri.fe.v;

import javafx.geometry.Pos;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Region;
import javafx.scene.layout.StackPane;
import lemuri.be.dto.Disk;
import lemuri.be.dto.MetricsResult;
import lemuri.fe.v.sections.UsageBar;

/** Disk icon and bar for a tree row, with no text: the colour tells how full the fullest disk is. Hidden without a reading. */
final class DiskMeter extends HBox {
    private static final double WIDTH = 40;
    private final Region value = new Region();
    private final Tooltip tip = new Tooltip();

    DiskMeter() {
        getStyleClass().add("disk-meter");
        value.getStyleClass().add("usage-bar-value");
        value.setMinWidth(0);
        StackPane track = new StackPane(value);
        track.getStyleClass().add("usage-bar-track");
        track.setAlignment(Pos.CENTER_LEFT);
        getChildren().addAll(Icons.icon("disk", 16), track);
        setMinWidth(Region.USE_PREF_SIZE);
        Tooltip.install(this, tip);
        managedProperty().bind(visibleProperty());
    }

    void show(MetricsResult e) {
        boolean known = e != null && e.resources() != null && !e.resources().disks().isEmpty();
        setVisible(known);
        if (!known) return;
        Disk fullest = e.resources().fullest();
        double q = Math.min(1, Math.max(0, fullest.quota()));
        value.setMaxWidth(WIDTH * q);
        value.getStyleClass().removeAll("normal", "warn", "critical");
        value.getStyleClass().add(UsageBar.level(q));
        tip.setText(UsageBar.disksTip(e));
    }
}
