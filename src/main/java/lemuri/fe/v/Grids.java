package lemuri.fe.v;

import java.util.List;
import java.util.function.Function;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.ReadOnlyObjectWrapper;
import javafx.beans.property.ReadOnlyStringWrapper;
import javafx.collections.ObservableList;
import javafx.scene.Node;
import javafx.scene.control.CheckBox;
import javafx.scene.control.TableCell;
import javafx.scene.control.TableColumn;
import javafx.scene.control.TableView;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Session;

/** Dense grids: tight rows, click to select, double click to open. */
public final class Grids {
    private Grids() {}

    public static <R> TableView<R> table(ObservableList<R> lines, String empty) {
        TableView<R> t = new TableView<>(lines);
        t.getStyleClass().add("grid");
        t.setPlaceholder(Ui.faint(empty));
        t.setColumnResizePolicy(TableView.CONSTRAINED_RESIZE_POLICY_FLEX_LAST_COLUMN);
        return t;
    }

    public static <R> TableColumn<R, String> column(String title, double width, Function<R, String> value) {
        TableColumn<R, String> c = new TableColumn<>(title);
        c.setPrefWidth(width);
        c.setCellValueFactory(x -> new ReadOnlyStringWrapper(value.apply(x.getValue())));
        return c;
    }

    /**
     * A column of check boxes bound to each line's property, with one in the header that ticks or unticks
     * them all (and shows whether they are all ticked).
     */
    public static <R> TableColumn<R, R> checkColumn(List<R> lines, Function<R, BooleanProperty> chosen) {
        TableColumn<R, R> c = nodeColumn("", 44, r -> {
            CheckBox b = new CheckBox();
            b.selectedProperty().bindBidirectional(chosen.apply(r));
            return b;
        });
        CheckBox all = new CheckBox();
        Runnable sync = () -> all.setSelected(!lines.isEmpty() && lines.stream().allMatch(r -> chosen.apply(r).get()));
        for (R r : lines) chosen.apply(r).addListener((o, a, b) -> sync.run());
        sync.run();
        all.setOnAction(e -> {
            boolean on = all.isSelected();
            for (R r : lines) chosen.apply(r).set(on);
        });
        c.setGraphic(all);
        c.setResizable(false);
        c.setStyle("-fx-alignment: CENTER;");
        return c;
    }

    /** Tall enough for every line up to {@code max} (header included), then it scrolls: no empty band, no cut line. */
    public static void fitRows(TableView<?> t, int lines, int max) {
        t.setFixedCellSize(32);
        double height = 44 + 32 * Math.max(1, Math.min(max, lines)) + 4;
        t.setPrefHeight(height);
        t.setMinHeight(height);
        t.setMaxHeight(height);
    }

    /** Column that shows a node (coloured status, code, buttons). */
    public static <R> TableColumn<R, R> nodeColumn(String title, double width, Function<R, Node> node) {
        TableColumn<R, R> c = new TableColumn<>(title);
        c.setPrefWidth(width);
        c.setCellValueFactory(x -> new ReadOnlyObjectWrapper<>(x.getValue()));
        c.setCellFactory(x -> new TableCell<>() {
            @Override
            protected void updateItem(R r, boolean empty) {
                super.updateItem(r, empty);
                setText(null);
                setGraphic(empty || r == null ? null : node.apply(r));
            }
        });
        c.setSortable(false);
        return c;
    }

    public static String duration(long from) {
        long s = Math.max(0, (System.currentTimeMillis() - from) / 1000);
        return String.format("%dh %02dm %02ds", s / 3600, (s % 3600) / 60, s % 60);
    }

    /** SSH sessions with destination, state and duration. */
    public static TableView<Session> sessionsTable(AppContext app, ObservableList<Session> lines) {
        TableView<Session> t = table(lines, I18n.t("sessions.none"));
        t.getColumns().add(column(I18n.t("sessions.col.session"), 150, s -> s.title));
        t.getColumns().add(nodeColumn(I18n.t("sessions.col.destination"), 260, s -> Ui.code(s.host.destination())));
        t.getColumns().add(nodeColumn(I18n.t("common.status"), 120, s -> Ui.sessionStatus(s.state.get())));
        t.getColumns().add(column(I18n.t("sessions.col.started"), 160, s -> Ui.dateTime(s.startup)));
        return t;
    }
}
