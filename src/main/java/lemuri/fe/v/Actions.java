package lemuri.fe.v;


import java.util.ArrayList;
import java.util.Comparator;
import java.util.List;
import java.util.Map;
import java.util.TreeMap;
import java.util.function.Supplier;
import javafx.event.Event;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.ContextMenu;
import javafx.scene.control.Menu;
import javafx.scene.control.MenuItem;
import javafx.scene.control.SeparatorMenuItem;
import javafx.scene.control.Tooltip;
import javafx.scene.input.ContextMenuEvent;
import javafx.scene.input.MouseButton;
import javafx.scene.input.MouseEvent;
import lemuri.be.dto.Host;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Session;
import lemuri.fe.v.terminal.Terminal;

/**
 * Operations on folders and hosts with their confirmations, used by the tree
 * context menu and by the buttons at the top of the central area.
 */
public final class Actions {
    private Actions() {}

    /** Name of a folder inside {@code parent}: a single segment, with no duplicates among its siblings. */
    private static String nameError(AppContext app, String parent, String name, String excluded) {
        if (name.isEmpty()) return I18n.t("folders.enterName");
        if (name.contains("/")) return I18n.t("folders.noSlash");
        String p = Folders.merge(parent, name);
        if (!p.equalsIgnoreCase(excluded) && app.state.folderExists(p)) return I18n.t("folders.exists");
        return null;
    }

    public static void newFolder(AppContext app, String parent) {
        String label = parent.isEmpty() ? I18n.t("folders.nameInDatacenter") : I18n.t("folders.nameIn", parent);
        Dialogs.name(I18n.t("folders.new"), label, "", I18n.t("vault.create.button"), n -> nameError(app, parent, n, null))
                .ifPresent(n -> {
                    String p = Folders.merge(parent, n);
                    app.hosts.createFolder(p);
                    app.state.select(new Selection.Folder(p), null);
                });
    }

    public static void renameFolder(AppContext app, String path) {
        String parent = Folders.parent(path);
        Dialogs.name(I18n.t("folders.rename.title"), I18n.t("folders.rename.label", path), Folders.name(path), I18n.t("folders.rename"),
                        n -> nameError(app, parent, n, path))
                .ifPresent(n -> app.hosts.moveFolder(path, Folders.merge(parent, n)));
    }

    public static void moveFolder(AppContext app, String path) {
        // It cannot end up inside itself or one of its subfolders.
        List<String> choices = new ArrayList<>();
        choices.add("");
        app.state.folders().stream().filter(c -> !Folders.inside(c, path)).forEach(choices::add);
        Dialogs.folder(I18n.t("folders.move.title"), I18n.t("folders.move.label", path), choices, Folders.parent(path))
                .ifPresent(dest -> move(app, path, dest));
    }

    /** Moves the folder under {@code dest}, unless a sibling with the same name is already there. */
    public static void move(AppContext app, String path, String dest) {
        if (dest.equals(Folders.parent(path)) || Folders.inside(dest, path)) return;
        String created = Folders.merge(dest, Folders.name(path));
        if (app.state.folderExists(created) && !Dialogs.confirm(I18n.t("folders.move.title"), I18n.t("folders.merge", Folders.label(dest), Folders.name(path)), I18n.t("folders.merge.button"))) return;
        app.hosts.moveFolder(path, created);
    }

    public static void deleteFolder(AppContext app, String path) {
        long n = app.state.hosts.stream().filter(h -> Folders.inside(h.group, path)).count();
        String on = Folders.label(Folders.parent(path));
        String text = n == 0
                ? I18n.t("folders.delete.empty", path)
                : n == 1 ? I18n.t("folders.delete.one", path, on) : I18n.t("folders.delete.many", path, n, on);
        if (Dialogs.confirm(I18n.t("folders.delete.title"), text, I18n.t("common.delete"))) app.hosts.deleteFolder(path);
    }

    public static void moveHost(AppContext app, Host h) {
        List<String> choices = new ArrayList<>();
        choices.add("");
        choices.addAll(app.state.folders());
        Dialogs.folder(I18n.t("hosts.move.title"), I18n.t("folders.move.label", h.name), choices, h.group)
                .ifPresent(dest -> app.hosts.moveHost(h, dest));
    }

    public static void removeHost(AppContext app, Host h) {
        if (app.state.connected(h.id) > 0) {
            Dialogs.warning(I18n.t("hosts.remove.title"), I18n.t("hosts.remove.busy", h.name));
            return;
        }
        if (Dialogs.confirm(I18n.t("hosts.remove.title"), I18n.t("hosts.remove.ask", h.name, h.address), I18n.t("common.remove"))) {
            app.hosts.removeHost(h);
            app.monitor.forget(h.id);
        }
    }

    // ── Context menus ────────────────────────────────────────────────────────

    public static ContextMenu menuDatacenter(AppContext app) {
        return Dialogs.style(new ContextMenu(
                item(I18n.t("hosts.add"), "plus", () -> HostDialog.forNew(app, "")),
                item(I18n.t("folders.new"), "folder", () -> newFolder(app, "")),
                item(I18n.t("sshImport.menu"), "download", () -> SshImportDialog.open(app, "")),
                new SeparatorMenuItem(),
                item(I18n.t("hosts.checkReachability"), "ok", app.monitor::tryAll)));
    }

    public static ContextMenu folderMenu(AppContext app, String path) {
        return Dialogs.style(new ContextMenu(
                item(I18n.t("hosts.addHere"), "plus", () -> HostDialog.forNew(app, path)),
                item(I18n.t("folders.newSub"), "folder", () -> newFolder(app, path)),
                item(I18n.t("sshImport.menu"), "download", () -> SshImportDialog.open(app, path)),
                new SeparatorMenuItem(),
                item(I18n.t("folders.renameMenu"), "edit", () -> renameFolder(app, path)),
                item(I18n.t("common.moveTo"), "right", () -> moveFolder(app, path)),
                new SeparatorMenuItem(),
                item(I18n.t("common.delete"), "trash", () -> deleteFolder(app, path))));
    }

    /**
     * Right click on a terminal. The right click must be stopped before it reaches the WebView: if it
     * gets it, it keeps the mouse until the button is released and the menu just opened does not see the
     * pointer moving over its items. {@code menu} returns null when there is nothing to offer.
     */
    public static void rightClick(Node zone, Supplier<ContextMenu> menu) {
        zone.addEventFilter(MouseEvent.MOUSE_PRESSED, e -> {
            if (e.getButton() != MouseButton.SECONDARY) return;
            e.consume();
            ContextMenu m = menu.get();
            if (m != null) m.show(zone, e.getScreenX(), e.getScreenY());
        });
        zone.addEventFilter(MouseEvent.MOUSE_RELEASED, e -> {
            if (e.getButton() == MouseButton.SECONDARY) e.consume();
        });
        // The menu already opened on press: the request that comes later must not open another one.
        zone.addEventFilter(ContextMenuEvent.CONTEXT_MENU_REQUESTED, Event::consume);
    }

    /**
     * Right click on a terminal (in the console and in ghost mode): split it, with a new session on the same
     * host on its right or below it, and upload into the shell's folder; a part of a split tab can be closed.
     */
    public static ContextMenu menuConsole(AppContext app, Session s) {
        MenuItem upload = item(I18n.t("terminal.upload"), "upload", () -> Transfers.uploadFromTerminal(s, null));
        upload.setDisable(!s.state.get().alive());
        ContextMenu m = new ContextMenu(
                copyItem(SessionViews.terminal(s)), pasteItem(SessionViews.terminal(s)),
                new SeparatorMenuItem(),
                splitMenu(app, s, true),
                splitMenu(app, s, false),
                new SeparatorMenuItem(),
                upload);
        // On a name listed by ls: download it (a file, or a folder with what is inside).
        String[] name = SessionViews.terminal(s).nameForMenu();
        if (name != null) {
            MenuItem download = item(I18n.t("terminal.download", name[0]), "download", () -> Transfers.downloadFromTerminal(s, name[0], name[1]));
            download.setDisable(!s.state.get().alive());
            m.getItems().add(download);
        }
        if (s.tab().panes().size() > 1) m.getItems().addAll(new SeparatorMenuItem(), item(I18n.t("terminal.closePane"), "close", () -> app.sessions.closePane(s)));
        return Dialogs.style(m);
    }

    /** Copy, on if something is selected in the terminal. */
    private static MenuItem copyItem(Terminal t) {
        MenuItem copy = item(I18n.t("terminal.copy"), "copy", t::copy);
        copy.setDisable(t.selection().isEmpty());
        return copy;
    }

    /** Paste, on if the clipboard has text. */
    private static MenuItem pasteItem(Terminal t) {
        MenuItem paste = item(I18n.t("terminal.paste"), "paste", t::pasteClipboard);
        paste.setDisable(Terminal.clipboardText() == null);
        return paste;
    }

    /** Right click on the local shell: copy and paste only. */
    public static ContextMenu menuText(Terminal t) {
        return Dialogs.style(new ContextMenu(copyItem(t), pasteItem(t)));
    }

    /**
     * Split right or down: first the same host (a second shell on it), then every other host, in the folders of
     * the tree, to put a connection to another server next to this one.
     */
    private static Menu splitMenu(AppContext app, Session s, boolean sideBySide) {
        Menu m = new Menu(I18n.t(sideBySide ? "terminal.splitRight" : "terminal.splitDown"),
                Icons.icon(sideBySide ? "split-right" : "split-down", 13));
        m.getItems().addAll(item(I18n.t("terminal.splitSameHost", s.host.name), "terminal", () -> app.sessions.split(s, sideBySide)),
                new SeparatorMenuItem());
        Map<String, Menu> folders = new TreeMap<>(String.CASE_INSENSITIVE_ORDER);
        List<MenuItem> loose = new ArrayList<>();
        app.state.hosts.stream().filter(h -> !h.id.equals(s.host.id))
                .sorted(Comparator.comparing((Host h) -> h.name, String.CASE_INSENSITIVE_ORDER))
                .forEach(h -> {
                    MenuItem i = item(h.name, "server", () -> app.sessions.split(s, sideBySide, h));
                    if (h.group == null || h.group.isBlank()) loose.add(i);
                    else folders.computeIfAbsent(h.group, g -> new Menu(g, Icons.icon("folder", 13))).getItems().add(i);
                });
        m.getItems().addAll(folders.values());
        m.getItems().addAll(loose);
        return m;
    }

    /** Disconnect, Edit, Move, Send and Remove: in the header of the host page and above its console. */
    public static List<Button> hostButtons(AppContext app, Host h) {
        return List.of(disconnectButton(app, h),
                Ui.button(I18n.t("common.edit"), "edit", () -> HostDialog.open(app, h)),
                Ui.button(I18n.t("common.move"), "right", () -> moveHost(app, h)),
                shareButton(app, h),
                removeButton(app, h));
    }

    /** Closes every session of the host; off when it has none. */
    /**
     * The commands of one terminal, in the transparent bar at its top right: the host's info (metrics), sync
     * typing (split tabs only, lit while on), share, close its sessions, remove it. Each part of a split tab
     * has those of its own host. Icons only: the name appears on hover.
     */
    public static List<Node> terminalButtons(AppContext app, Session s) {
        Host h = s.host;
        List<Node> out = new ArrayList<>();
        out.add(Ui.action("summary", I18n.t("console.summaryOf", h.name), () -> app.state.select(new Selection.HostItem(h.id), "summary")));
        Session tab = s.tab();
        if (tab.panes().size() > 1) {
            Button sync = Ui.action("sync", I18n.t(tab.synced.get() ? "console.sync.off" : "console.sync.on"), () -> tab.synced.set(!tab.synced.get()));
            if (tab.synced.get()) sync.getStyleClass().add("on");
            out.add(sync);
        }
        for (Button b : List.of(shareButton(app, h), disconnectButton(app, h), removeButton(app, h))) {
            if (b.getTooltip() == null) b.setTooltip(new Tooltip(b.getText() + " " + h.name));
            b.setText(null);
            b.getStyleClass().add("action");
            out.add(b);
        }
        return out;
    }

    public static Button disconnectButton(AppContext app, Host h) {
        Button b = Ui.button(I18n.t("hosts.disconnect"), "power", () -> app.sessions.disconnectHost(h.id));
        b.setDisable(app.state.connected(h.id) == 0);
        return b;
    }

    public static Button shareButton(AppContext app, Host h) {
        return Ui.button(I18n.t("hosts.send"), "share", () -> SharingDialogs.send(app, h));
    }

    /** Removes the host; off while it has open sessions, with the reason on hover. */
    public static Button removeButton(AppContext app, Host h) {
        long connected = app.state.connected(h.id);
        Button b = Ui.danger(I18n.t("common.remove"), "trash", () -> removeHost(app, h));
        b.setDisable(connected > 0);
        if (connected > 0) b.setTooltip(new Tooltip(I18n.t("hosts.remove.closeFirst")));
        return b;
    }

    public static ContextMenu menuHost(AppContext app, Host h) {
        MenuItem disconnect = item(I18n.t("hosts.disconnect"), "power", () -> app.sessions.disconnectHost(h.id));
        disconnect.setDisable(app.state.connected(h.id) == 0);
        return Dialogs.style(new ContextMenu(
                item(I18n.t("hosts.openConsole"), "terminal", () -> app.sessions.showOrOpen(h)),
                disconnect,
                new SeparatorMenuItem(),
                item(I18n.t("hosts.editMenu"), "edit", () -> HostDialog.open(app, h)),
                item(I18n.t("hosts.duplicate"), "plus", () -> HostDialog.duplicate(app, h)),
                item(I18n.t("common.moveTo"), "right", () -> moveHost(app, h)),
                item(I18n.t("hosts.sendTeam"), "share", () -> SharingDialogs.send(app, h)),
                new SeparatorMenuItem(),
                item(I18n.t("common.remove"), "trash", () -> removeHost(app, h))));
    }

    private static MenuItem item(String text, String icon, Runnable action) {
        MenuItem m = new MenuItem(text, Icons.icon(icon, 13));
        m.setOnAction(e -> action.run());
        return m;
    }
}
