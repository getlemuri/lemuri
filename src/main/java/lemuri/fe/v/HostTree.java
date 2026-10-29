package lemuri.fe.v;

import java.util.ArrayList;
import java.util.Comparator;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.TreeMap;
import java.util.TreeSet;
import javafx.application.Platform;
import javafx.beans.Observable;
import javafx.beans.property.SimpleStringProperty;
import javafx.beans.property.StringProperty;
import javafx.collections.ListChangeListener;
import javafx.geometry.Pos;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.Label;
import javafx.scene.control.Tooltip;
import javafx.scene.control.TreeCell;
import javafx.scene.control.TreeItem;
import javafx.scene.control.TreeView;
import javafx.scene.input.ClipboardContent;
import javafx.scene.input.DataFormat;
import javafx.scene.input.Dragboard;
import javafx.scene.input.KeyCode;
import javafx.scene.input.MouseButton;
import javafx.scene.input.TransferMode;
import javafx.scene.layout.HBox;
import lemuri.be.dto.Host;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Session;

/**
 * Resource tree: the datacenter, then the nested folders and the
 * hosts inside. A double click on a host opens an SSH console right away; a click on a host with an open
 * session brings back its shell. Right click for the operations; hosts and folders can be dragged onto
 * another folder or onto the datacenter.
 */
public class HostTree extends TreeView<HostTree.Item> {
    public record Item(String key, Selection node, String text, String icon, String cssClass, Host host, int tally, boolean branch) {}

    public final StringProperty filter = new SimpleStringProperty("");
    private final AppContext app;
    private static final DataFormat DRAGGED = new DataFormat("application/x-lemuri-node");

    private final Set<String> closedOnes = new HashSet<>();
    private boolean syncing;

    public HostTree(AppContext app) {
        this.app = app;
        getStyleClass().add("tree");
        // Hidden root: below it are the datacenter and, if any, the hosts received from contacts.
        setShowRoot(false);
        setCellFactory(t -> new ItemCell());

        filter.addListener(o -> rebuild());
        app.state.hosts.addListener((Observable o) -> rebuild());
        app.state.createdFolders.addListener((Observable o) -> rebuild());
        app.state.sessions.addListener((ListChangeListener<Session>) c -> {
            while (c.next()) for (Session s : c.getAddedSubList()) s.state.addListener(o -> rebuild());
            rebuild();
        });
        app.state.position.addListener(o -> syncSelection());
        rebuild();
    }

    private static boolean matches(Host h, String text) {
        return text.isEmpty() || h.name.toLowerCase(Locale.ROOT).contains(text) || h.address.toLowerCase(Locale.ROOT).contains(text)
                || h.group.toLowerCase(Locale.ROOT).contains(text);
    }

    private TreeItem<Item> leaf(String key, Host h) {
        return new TreeItem<>(new Item(key + "/" + h.id, new Selection.HostItem(h.id), h.name, "server",
                Ui.hostStatusClass(h.state), h, (int) app.state.connected(h.id), false));
    }

    private void rebuild() {
        String text = filter.get().trim().toLowerCase(Locale.ROOT);
        TreeItem<Item> dc = item(new Item("dc", new Selection.Datacenter(), I18n.t("tree.datacenter"), "datacenter", null, null, 0, true), text);
        folders(dc, text);
        TreeItem<Item> root = new TreeItem<>(new Item("root", null, "", "datacenter", null, null, 0, true));
        root.setExpanded(true);
        root.getChildren().add(dc);
        TreeItem<Item> received = received(text);
        if (received != null) root.getChildren().add(received);
        syncing = true;
        setRoot(root);
        syncing = false;
        syncSelection();
    }

    /** Hosts arrived from contacts and not yet sorted, grouped by who sent them. */
    private TreeItem<Item> received(String text) {
        Map<String, List<Host>> byContact = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
        for (Host h : app.state.hosts)
            if (h.received && matches(h, text)) byContact.computeIfAbsent(h.fromContact != null ? h.fromContact : "?", k -> new ArrayList<>()).add(h);
        if (byContact.isEmpty()) return null;
        int n = byContact.values().stream().mapToInt(List::size).sum();
        TreeItem<Item> section = item(new Item("received", new Selection.Received(), I18n.t("tree.received"), "user", null, null, n, true), text);
        byContact.forEach((from, hosts) -> {
            TreeItem<Item> branch = item(new Item("received:" + from, new Selection.Received(), from, "user", "folder-icon", null, hosts.size(), true), text);
            hosts.sort(Comparator.comparing((Host h) -> h.name, String.CASE_INSENSITIVE_ORDER));
            for (Host h : hosts) branch.getChildren().add(leaf("received:" + from, h));
            section.getChildren().add(branch);
        });
        return section;
    }

    /** Folder view: first the subfolders, then the hosts, at every level. */
    private void folders(TreeItem<Item> dc, String text) {
        // As in app.state.folders(): "prod" and "Prod" are the same folder.
        Map<String, TreeItem<Item>> branches = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
        branches.put("", dc);
        // With the filter, keep the matching folders, those with matching hosts and their ancestors.
        Set<String> kept = new TreeSet<>(String.CASE_INSENSITIVE_ORDER);
        if (!text.isEmpty()) {
            List<String> foundOnes = new ArrayList<>(app.state.folders().stream().filter(c -> c.toLowerCase(Locale.ROOT).contains(text)).toList());
            app.state.hosts.stream().filter(h -> matches(h, text)).forEach(h -> foundOnes.add(h.group));
            for (String c : foundOnes) for (String p = c; !p.isEmpty(); p = Folders.parent(p)) kept.add(p);
        }
        for (String c : app.state.folders()) {
            if (!text.isEmpty() && !kept.contains(c)) continue;
            long n = app.state.hosts.stream().filter(h -> !h.received && Folders.inside(h.group, c)).count();
            TreeItem<Item> branch = item(new Item("folder:" + c, new Selection.Folder(c), Folders.name(c), "folder",
                    "folder-icon", null, (int) n, true), text);
            branches.put(c, branch);
            // app.state.folders() is sorted and contains the ancestors: the parent is already in place.
            TreeItem<Item> parent = branches.get(Folders.parent(c));
            if (parent != null) parent.getChildren().add(branch);
        }
        List<Host> sorted = app.state.hosts.stream().sorted(Comparator.comparing((Host h) -> h.name, String.CASE_INSENSITIVE_ORDER)).toList();
        for (Host h : sorted) {
            TreeItem<Item> branch = branches.get(h.group);
            if (branch != null && !h.received && matches(h, text)) branch.getChildren().add(leaf(h.group, h));
        }
    }

    private TreeItem<Item> item(Item v, String text) {
        TreeItem<Item> t = new TreeItem<>(v);
        // With an active filter the branches stay open, otherwise the results would be hidden.
        t.setExpanded(!text.isEmpty() || !closedOnes.contains(v.key()));
        t.expandedProperty().addListener((o, before, now) -> {
            if (syncing) return;
            if (now) closedOnes.remove(v.key());
            else closedOnes.add(v.key());
        });
        return t;
    }

    /** Expands or collapses all folders under the datacenter. */
    public void expandAll(boolean opened) {
        if (getRoot() == null) return;
        for (TreeItem<Item> dc : getRoot().getChildren()) expand(dc, opened);
    }

    private void expand(TreeItem<Item> t, boolean opened) {
        for (TreeItem<Item> f : t.getChildren()) {
            if (f.isLeaf()) continue;
            f.setExpanded(opened);
            expand(f, opened);
        }
    }

    /** The highlight always follows the node selected in the state, even after an ignored click. */
    private void syncSelection() {
        Selection s = app.state.node();
        TreeItem<Item> found = search(getRoot(), s);
        syncing = true;
        if (found != null) getSelectionModel().select(found);
        else getSelectionModel().clearSelection();
        syncing = false;
    }

    private TreeItem<Item> search(TreeItem<Item> t, Selection s) {
        if (t == null) return null;
        if (s.equals(t.getValue().node())) return t;
        for (TreeItem<Item> f : t.getChildren()) {
            TreeItem<Item> r = search(f, s);
            if (r != null) return r;
        }
        return null;
    }

    private void choose(Item v) {
        // A host with an open session brings you back to its shell, from any page; the summary opens from the console.
        if (v.host() != null) {
            Session s = app.state.activeSession.get(v.host().id);
            if (s != null) {
                app.sessions.showSession(s);
                return;
            }
        }
        if (v.node() != null) app.state.select(v.node(), null);
        else syncSelection();
    }

    private void open(Item v) {
        if (v.host() == null) return;
        app.sessions.showOrOpen(v.host());
    }

    /** Folder where something can be dropped: "" is the datacenter, null none (hosts). */
    private String destination(Item v) {
        if (v == null) return null;
        if (v.node() instanceof Selection.Datacenter) return "";
        if (v.node() instanceof Selection.Folder c) return c.path();
        return null;
    }

    private boolean accept(String dragged, String dest) {
        if (dragged == null || dest == null) return false;
        if (dragged.startsWith("host:")) {
            Host h = app.state.host(dragged.substring(5));
            return h != null && (h.received || !h.group.equals(dest));
        }
        String c = dragged.substring("folder:".length());
        return !dest.equals(Folders.parent(c)) && !Folders.inside(dest, c);
    }

    private final class ItemCell extends TreeCell<Item> {
        ItemCell() {
            setOnDragDetected(e -> {
                Item v = getItem();
                if (isEmpty() || v == null) return;
                String id = v.host() != null ? "host:" + v.host().id : v.node() instanceof Selection.Folder c ? "folder:" + c.path() : null;
                if (id == null) return;
                Dragboard db = startDragAndDrop(TransferMode.MOVE);
                ClipboardContent cc = new ClipboardContent();
                cc.put(DRAGGED, id);
                db.setContent(cc);
                e.consume();
            });
            setOnDragOver(e -> {
                if (accept((String) e.getDragboard().getContent(DRAGGED), destination(getItem()))) e.acceptTransferModes(TransferMode.MOVE);
                e.consume();
            });
            setOnDragEntered(e -> {
                if (accept((String) e.getDragboard().getContent(DRAGGED), destination(getItem()))) getStyleClass().add("tree-target");
            });
            setOnDragExited(e -> getStyleClass().remove("tree-target"));
            setOnDragDropped(e -> {
                String t = (String) e.getDragboard().getContent(DRAGGED);
                String dest = destination(getItem());
                boolean ok = accept(t, dest);
                e.setDropCompleted(ok);
                e.consume();
                if (!ok) return;
                // After the drop: confirmation dialogs must not open during the drag.
                Platform.runLater(() -> {
                    if (t.startsWith("host:")) app.hosts.moveHost(app.state.host(t.substring(5)), dest);
                    else Actions.move(app, t.substring("folder:".length()), dest);
                });
            });
            setOnContextMenuRequested(e -> {
                Item v = getItem();
                if (isEmpty() || v == null) return;
                ContextMenu m = v.host() != null ? Actions.menuHost(app, v.host())
                        : v.node() instanceof Selection.Folder c ? Actions.folderMenu(app, c.path())
                        : v.node() instanceof Selection.Datacenter ? Actions.menuDatacenter(app)
                        : null;
                if (m != null) m.show(this, e.getScreenX(), e.getScreenY());
                e.consume();
            });
            setOnMouseClicked(e -> {
                if (isEmpty() || getItem() == null || e.getButton() != MouseButton.PRIMARY) return;
                if (e.getClickCount() == 2) open(getItem());
                else choose(getItem());
            });
            setOnKeyPressed(e -> {
                if (e.getCode() == KeyCode.ENTER && getItem() != null) {
                    if (getItem().host() != null) open(getItem());
                    else choose(getItem());
                }
            });
        }

        @Override
        protected void updateItem(Item v, boolean empty) {
            super.updateItem(v, empty);
            if (empty || v == null) {
                setText(null);
                setGraphic(null);
                setTooltip(null);
                return;
            }
            Label text = new Label(v.text());
            text.setMinWidth(0);
            // An open folder shows the open icon.
            String icon = v.icon().equals("folder") && getTreeItem() != null && getTreeItem().isExpanded() ? "folder-open" : v.icon();
            HBox line = new HBox(8, Icons.icon(icon, 16, v.cssClass() != null ? new String[] {v.cssClass()} : new String[0]), text);
            line.setAlignment(Pos.CENTER_LEFT);
            line.getStyleClass().add("tree-row");
            // Datacenter, contacts and received are the top-level items: bolder text.
            if (getTreeItem() != null && getTreeItem().getParent() == getTreeView().getRoot()) line.getStyleClass().add("tree-root");
            // Folders show no totals; a host shows how many sessions it has open.
            if (!v.branch() && v.tally() > 0) {
                Label n = new Label(String.valueOf(v.tally()));
                n.getStyleClass().add("tree-sessions");
                line.getChildren().addAll(Ui.space(), n);
            }
            setText(null);
            setGraphic(line);
            setTooltip(v.host() != null ? new Tooltip(I18n.t("tree.hostTip", v.host().destination())) : null);
        }
    }
}
