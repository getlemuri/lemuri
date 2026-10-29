package lemuri.fe.v.sections;

import java.util.List;
import javafx.animation.KeyFrame;
import javafx.animation.Timeline;
import javafx.collections.MapChangeListener;
import javafx.collections.WeakMapChangeListener;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Label;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Pane;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.scene.shape.Rectangle;
import javafx.util.Duration;
import lemuri.be.dto.Disk;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.dto.MetricsResult;
import lemuri.be.dto.Resources;
import lemuri.be.dto.Updates;
import lemuri.be.service.MetricsService;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.v.Ui;

/**
 * Host summary: details with pending updates and reboot, CPU/RAM/disk rings, disks and CPU columns. The
 * numbers come from the VPS over SSH (see {@link lemuri.be.monitoring.Metrics}), read again every
 * {@value #EVERY_S} seconds while the view is open.
 */
public final class HostSummary extends VBox {
    private static final int EVERY_S = 4;

    private final AppContext app;
    private final Host h;
    private final Pane columns = new Pane();
    private final Gauge gCpu = new Gauge("CPU");
    private final Gauge gRam = new Gauge(I18n.t("summary.memory"));
    private final Gauge gDisk = new Gauge(I18n.t("summary.diskRoot"));
    private final Label note = Ui.faint("");
    private final Label enabled = new Label();
    private final VBox disks = new VBox();
    /** Filled when the server answers: counting packages takes it a few seconds. */
    private final HBox updatesBox = new HBox(6), rebootBox = new HBox(6);
    private final Timeline round;
    /** Strong reference for the weak listener on the resources. */
    private final MapChangeListener<String, MetricsResult> onResources;

    public HostSummary(AppContext app, Host h) {
        this.app = app;
        this.h = h;
        getStyleClass().add("section");

        String auth = switch (h.authMethod) {
            // Without a key file of its own, SSH tries the agent and then the keys in ~/.ssh.
            case KEY -> h.key == null || h.key.isBlank() ? I18n.t("summary.auth.keyAuto") : I18n.t("summary.auth.key", h.key);
            case AGENT -> I18n.t("auth.agent");
            case PASSWORD -> I18n.t("summary.auth.password");
        };
        for (HBox b : List.of(updatesBox, rebootBox)) b.setAlignment(Pos.CENTER_LEFT);
        Node details = Ui.details(List.of(
                new Ui.Detail(I18n.t("common.status"), Ui.hostStatus(h.state)),
                new Ui.Detail(I18n.t("summary.updates"), updatesBox),
                new Ui.Detail(I18n.t("summary.reboot"), rebootBox),
                new Ui.Detail(I18n.t("host.address"), Ui.code(h.address + ":" + h.port)),
                new Ui.Detail(I18n.t("host.user"), new Label(h.user)),
                new Ui.Detail(I18n.t("summary.jump"), h.jump != null && !h.jump.isBlank() ? Ui.code(h.jump) : null),
                new Ui.Detail(I18n.t("host.auth"), new Label(auth)),
                new Ui.Detail(I18n.t("host.os"), h.os != null ? new Label(h.os) : null),
                new Ui.Detail(I18n.t("summary.upFor"), enabled),
                new Ui.Detail(I18n.t("host.folder"), new Label(Folders.label(h.group))),
                new Ui.Detail(I18n.t("summary.connected"), new Label(String.valueOf(app.state.connected(h.id))))));

        HBox rings = new HBox(gCpu, gRam, gDisk);
        rings.getStyleClass().add("resources");
        for (Node n : rings.getChildren()) {
            HBox.setHgrow(n, Priority.ALWAYS);
            ((VBox) n).setMaxWidth(Double.MAX_VALUE);
        }
        note.setWrapText(true);
        VBox resources = new VBox(8, rings, note);

        HBox line = new HBox(8, Ui.panel(I18n.t("common.status"), details), Ui.panel(I18n.t("summary.resources"), resources));
        for (Node n : line.getChildren()) {
            HBox.setHgrow(n, Priority.ALWAYS);
            ((VBox) n).setPrefWidth(0);
        }
        disks.getStyleClass().add("disks");
        columns.setPrefHeight(120);
        columns.setMinHeight(120);
        columns.widthProperty().addListener(o -> draw());
        getChildren().addAll(line, Ui.panel(I18n.t("summary.disks"), disks),
                Ui.panel(I18n.t("summary.cpuHistory", MetricsService.SAMPLES * EVERY_S / 60), columns),
                Ui.panel(I18n.t("summary.notes"), new HostNotes(app, h)));

        onResources = c -> {
            if (c.getKey().equals(h.id)) show();
        };
        app.state.resources.addListener(new WeakMapChangeListener<>(onResources));
        int[] ticks = {0};
        round = new Timeline(new KeyFrame(Duration.seconds(EVERY_S), e -> {
            app.monitor.refreshMetrics(h, false);
            // The service keeps the updates for minutes: asking once a minute costs nothing in between.
            if (++ticks[0] % 15 == 0) app.monitor.updates(h, false, this::showUpdates);
        }));
        round.setCycleCount(Timeline.INDEFINITE);
        showUpdates(null);
        if (h.state != HostStatus.OFFLINE) {
            // Without forcing: the view is rebuilt often and a host that refuses access must be left alone.
            app.monitor.refreshMetrics(h, false);
            app.monitor.updates(h, false, this::showUpdates);
            round.play();
        }
        // Stops the timer when the view leaves the scene.
        sceneProperty().addListener((o, before, now) -> {
            if (now == null) round.stop();
        });
        show();
    }

    /** Pending updates and reboot; null while the server has not answered yet. */
    private void showUpdates(Updates u) {
        if (u == null) {
            String text = h.state == HostStatus.OFFLINE ? I18n.t("summary.unreachable") : I18n.t("summary.updates.checking");
            updatesBox.getChildren().setAll(Ui.state("neutral", text));
            rebootBox.getChildren().setAll(Ui.state("neutral", text));
            return;
        }
        if (u.error() != null) {
            updatesBox.getChildren().setAll(Ui.faint(u.error()));
            rebootBox.getChildren().setAll(Ui.faint(u.error()));
            return;
        }
        if (u.packages() == null) updatesBox.getChildren().setAll(Ui.state("neutral", I18n.t("summary.updates.unknown")));
        else if (u.packages() == 0) updatesBox.getChildren().setAll(Ui.state("ok", I18n.t("summary.updates.none")));
        else {
            updatesBox.getChildren().setAll(Ui.state("warn", I18n.t("summary.updates.count", u.packages())));
            if (u.security() != null && u.security() > 0) updatesBox.getChildren().add(Ui.state("error", I18n.t("summary.updates.security", u.security())));
        }
        if (u.reboot() == null) rebootBox.getChildren().setAll(Ui.state("neutral", I18n.t("summary.reboot.unknown")));
        else if (!u.reboot()) rebootBox.getChildren().setAll(Ui.state("ok", I18n.t("summary.reboot.no")));
        else {
            rebootBox.getChildren().setAll(Ui.state("error", I18n.t("summary.reboot.needed")));
            if (u.rebootBecause() != null) {
                // Which packages ask for it (often the kernel): cut if long, whole in the tooltip.
                Label why = Ui.faint(u.rebootBecause());
                why.setTooltip(new Tooltip(u.rebootBecause()));
                why.setMinWidth(0);
                rebootBox.getChildren().add(why);
            }
        }
    }

    private void show() {
        MetricsResult e = app.state.resources.get(h.id);
        Resources r = e != null ? e.resources() : null;
        if (r == null) {
            String why = h.state == HostStatus.OFFLINE ? I18n.t("summary.unreachable")
                    : e != null && e.error() != null ? e.error() : I18n.t("metrics.reading");
            gCpu.set(null, "");
            gRam.set(null, "");
            gDisk.set(null, "");
            note.setText(why);
            enabled.setText("");
            disks.getChildren().setAll(Ui.faint(why));
            draw();
            return;
        }
        gCpu.set(r.cpuUsage(), r.cpu() + " vCPU");
        long used = r.memTotalKb() - r.memFreeKb();
        gRam.set(r.memTotalKb() > 0 ? (double) used / r.memTotalKb() : null,
                I18n.t("summary.usedOf", UsageBar.measure(used), UsageBar.measure(r.memTotalKb())));
        Disk root = r.root();
        gDisk.set(root != null ? root.quota() : null,
                root != null ? I18n.t("summary.usedOf", UsageBar.measure(root.usedKb()), UsageBar.measure(root.totalKb())) : "");
        enabled.setText(duration(r.onForS()));
        note.setText(e.error() != null ? I18n.t("metrics.stale", e.error()) : "");

        disks.getChildren().clear();
        for (Disk d : r.disks()) {
            Label dot = Ui.code(d.dot());
            dot.getStyleClass().add("disk-dot");
            UsageBar b = new UsageBar(d.quota(), I18n.t("summary.diskLine", UsageBar.measure(d.usedKb()), UsageBar.measure(d.totalKb()),
                    UsageBar.measure(d.freeKb())), 260);
            HBox line = new HBox(12, dot, b);
            line.setAlignment(Pos.CENTER_LEFT);
            disks.getChildren().add(line);
        }
        draw();
    }

    private static String duration(long s) {
        long g = s / 86_400, o = s % 86_400 / 3600, m = s % 3600 / 60;
        return g > 0 ? (g == 1 ? I18n.t("uptime.day", g, o) : I18n.t("uptime.days", g, o)) : o > 0 ? I18n.t("uptime.hours", o, m) : I18n.t("uptime.minutes", m);
    }

    private void draw() {
        List<Double> cpu = app.monitor.cpuHistory(h.id);
        double w = columns.getWidth(), alt = 116, wid = w / MetricsService.SAMPLES;
        columns.getChildren().clear();
        // New samples enter from the right and scroll to the left.
        int first = MetricsService.SAMPLES - cpu.size();
        for (int i = 0; i < cpu.size(); i++) {
            double hh = Math.max(1, cpu.get(i) * alt);
            Rectangle r = new Rectangle((first + i) * wid + wid * 0.12, 120 - hh, Math.max(1, wid * 0.76), hh);
            r.getStyleClass().add("column");
            columns.getChildren().add(r);
        }
        // Drawn again at every reading: styled now, not at the next pulse (they would flash black).
        if (getScene() != null) columns.applyCss();
    }
}
