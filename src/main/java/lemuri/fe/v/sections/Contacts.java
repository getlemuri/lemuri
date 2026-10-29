package lemuri.fe.v.sections;

import java.io.IOException;
import javafx.beans.InvalidationListener;
import javafx.beans.Observable;
import javafx.beans.WeakInvalidationListener;
import javafx.beans.binding.Bindings;
import javafx.collections.ListChangeListener;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.Label;
import javafx.scene.control.TableView;
import javafx.scene.control.Tooltip;
import javafx.scene.layout.HBox;
import javafx.scene.layout.VBox;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Peer;
import lemuri.be.dto.TrustedPeer;
import lemuri.be.service.LanService;
import lemuri.be.service.SharingService;
import lemuri.be.sharing.Envelope;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.c.LanController;
import lemuri.fe.c.SharingController;
import lemuri.fe.v.Dialogs;
import lemuri.fe.v.Grids;
import lemuri.fe.v.Icons;
import lemuri.fe.v.SharingDialogs;
import lemuri.fe.v.Ui;

/**
 * Sharing with contacts: this user's key, the incoming hosts to accept or reject, the contacts
 * (keys we trust) and the PCs on the local network.
 */
public final class Contacts extends VBox {
    private final AppContext app;
    private final SharingController c;
    private final HBox statusActions = new HBox(8);
    private final Label stateTone = new Label();
    /** Strong reference for the weak listener on the sharing state. */
    private final InvalidationListener onState = o -> refreshStatus();

    public Contacts(AppContext app) {
        this.app = app;
        this.c = app.sharing;
        getStyleClass().add("section");
        setSpacing(8);

        // ── You ──
        Label message = new Label();
        message.textProperty().bind(c.message);
        message.setWrapText(true);
        Label fingerprint = Ui.faint("");
        HBox head = new HBox(10, stateTone, message);
        head.setAlignment(Pos.CENTER_LEFT);
        statusActions.setAlignment(Pos.CENTER_LEFT);
        Label howItWorks = Ui.faint(I18n.t("team.howItWorks"));
        howItWorks.setWrapText(true);
        VBox you = new VBox(8, head, fingerprint, statusActions, howItWorks);
        c.status.addListener(new WeakInvalidationListener(onState));
        refreshStatus();
        fingerprint.textProperty().bind(Bindings.createStringBinding(
                () -> c.myFingerprint() == null ? "" : I18n.t("team.yourFingerprint", c.myFingerprint()),
                c.status));

        // ── Incoming ───
        TableView<Delivery> arrivals = Grids.table(c.incoming, I18n.t("team.incoming.none"));
        arrivals.getColumns().add(Grids.column(I18n.t("team.col.from"), 150, i -> i.from()));
        // The name alone is not proof: the fingerprint says which key the host really came from.
        arrivals.getColumns().add(Grids.nodeColumn(I18n.t("team.col.fingerprint"), 310,
                i -> i.fromKey() != null ? Ui.code(Envelope.fingerprint(i.fromKey())) : Ui.faint("?")));
        arrivals.getColumns().add(Grids.column(I18n.t("common.host"), 140, i -> i.content() != null ? i.content().name : "?"));
        arrivals.getColumns().add(Grids.nodeColumn(I18n.t("host.address"), 190, i -> i.content() != null
                ? Ui.code(i.content().user + "@" + i.content().address + ":" + i.content().port) : Ui.faint(i.error())));
        arrivals.getColumns().add(Grids.nodeColumn(I18n.t("auth.password"), 80, i -> i.content() == null ? new Label("")
                : i.content().password != null ? Ui.state("info", I18n.t("team.password.included")) : Ui.state("neutral", I18n.t("team.password.no"))));
        arrivals.getColumns().add(Grids.column(I18n.t("team.col.message"), 180, i -> i.content() != null && i.content().message != null ? i.content().message : ""));
        arrivals.getColumns().add(Grids.column(I18n.t("team.col.received"), 120, i -> Ui.dateTime(i.when())));
        arrivals.getColumns().add(Grids.nodeColumn("", 230, i -> {
            Button accept = Ui.primary(I18n.t("team.accept"), "ok", () -> SharingDialogs.accept(app, i));
            accept.setDisable(i.content() == null);
            Button reject = Ui.button(I18n.t("team.reject"), "close", () -> {
                if (Dialogs.confirm(I18n.t("team.reject"), I18n.t("team.reject.ask", i.from()), I18n.t("team.reject"))) SharingDialogs.remove(c, i);
            });
            return new HBox(6, accept, reject);
        }));
        arrivals.setMinHeight(150);
        arrivals.setPrefHeight(170);
        Button paste = Ui.primary(I18n.t("paste.button"), "inbox", () -> SharingDialogs.paste(app));

        // ── Contacts ──
        LanController lan = c.lan;
        TableView<TrustedPeer> contacts = Grids.table(c.contacts(), I18n.t("contacts.none"));
        contacts.getColumns().add(Grids.nodeColumn(I18n.t("common.status"), 130, t -> lan.reachable(t.key()) != null
                ? Ui.state("ok", I18n.t("contacts.nearby")) : Ui.state("neutral", I18n.t("contacts.byText"))));
        contacts.getColumns().add(Grids.column(I18n.t("host.name"), 220, TrustedPeer::name));
        contacts.getColumns().add(Grids.nodeColumn(I18n.t("team.col.fingerprint"), 310, t -> Ui.code(Envelope.fingerprint(t.key()))));
        contacts.getColumns().add(Grids.nodeColumn("", 120, t -> Ui.button(I18n.t("common.remove"), "trash", () -> {
            if (!Dialogs.confirm(I18n.t("contacts.remove"), I18n.t("contacts.remove.ask", t.name()), I18n.t("common.remove"))) return;
            try {
                lan.forget(t.key());
            } catch (IOException | IllegalStateException e) {
                Dialogs.warning(I18n.t("contacts.remove"), e.getMessage());
            }
        })));
        // "Nearby" changes with the PCs seen on the network, not only with the list.
        lan.peer.addListener((ListChangeListener<Peer>) x -> contacts.refresh());
        // As tall as the contacts (up to eight, then it scrolls): no contact cut in half.
        Grids.fitRows(contacts, contacts.getItems().size(), 8);
        contacts.getItems().addListener((Observable o) -> Grids.fitRows(contacts, contacts.getItems().size(), 8));
        VBox contactsBox = new VBox(8, contacts, new HBox(8,
                Ui.button(I18n.t("contact.title"), "plus", () -> SharingDialogs.addContact(app)),
                Ui.button(I18n.t("mykey.title"), "right", () -> SharingDialogs.myKey(app)),
                Ui.faint(I18n.t("contacts.note"))));

        // ── Local network ──
        Label lanState = new Label();
        lanState.textProperty().bind(lan.message);
        lanState.setWrapText(true);
        CheckBox isOn = new CheckBox(I18n.t("lan.enable"));
        isOn.setSelected(lan.isOn());
        isOn.setOnAction(e -> lan.turnOn(isOn.isSelected()));
        TableView<Peer> nearby = Grids.table(lan.peer, I18n.t("lan.none"));
        nearby.getColumns().add(Grids.nodeColumn(I18n.t("common.status"), 130, p -> {
            Label l = p.warning() != null && p.nearby() ? Ui.state("error", I18n.t("lan.firewall"))
                    : lan.trusted(p.key()) ? Ui.state(p.nearby() ? "ok" : "neutral", p.nearby() ? I18n.t("lan.linked") : I18n.t("lan.linkedAway"))
                    : Ui.state("warn", I18n.t("lan.notLinked"));
            // A plug in for the PCs connected, pulled out for the others.
            l.setGraphic(Icons.icon(lan.trusted(p.key()) ? "plug" : "plug-off", 12));
            l.setGraphicTextGap(5);
            if (p.warning() != null && p.nearby()) l.setTooltip(new Tooltip(p.who() + ": " + p.warning()));
            return l;
        }));
        nearby.getColumns().add(Grids.column(I18n.t("host.name"), 200, p -> lan.trustedName(p.key()) != null ? lan.trustedName(p.key()) : p.who()));
        nearby.getColumns().add(Grids.column(I18n.t("host.address"), 130, p -> p.address()));
        nearby.getColumns().add(Grids.nodeColumn(I18n.t("team.col.fingerprint"), 310, p -> Ui.code(p.fingerprint())));
        nearby.getColumns().add(Grids.nodeColumn("", 120, p -> {
            if (lan.trusted(p.key())) return Ui.button(I18n.t("lan.unlink"), "plug-off", () -> {
                if (!Dialogs.confirm(I18n.t("lan.unlink"), I18n.t("lan.unlink.ask", p.who()), I18n.t("lan.unlink"))) return;
                try {
                    lan.forget(p.key());
                } catch (IOException | IllegalStateException e) {
                    Dialogs.warning(I18n.t("lan.unlink"), e.getMessage());
                }
            });
            return Ui.primary(I18n.t("lan.link"), "plug", () -> SharingDialogs.pair(app, p));
        }));
        // The "linked" state also depends on the contacts, not only on the list.
        lan.trusted.addListener((ListChangeListener<TrustedPeer>) x -> nearby.refresh());
        nearby.setMinHeight(120);
        nearby.setPrefHeight(140);
        // This PC's firewall, if it might keep the others out; and the others' one, from the list.
        Label firewallWarning = new Label();
        firewallWarning.getStyleClass().add("warn-text");
        firewallWarning.setWrapText(true);
        firewallWarning.textProperty().bind(lan.firewall);
        firewallWarning.visibleProperty().bind(lan.firewall.isNotEmpty());
        firewallWarning.managedProperty().bind(firewallWarning.visibleProperty());
        Label notifyNearby = new Label();
        notifyNearby.getStyleClass().add("warn-text");
        notifyNearby.setWrapText(true);
        Runnable onNearby = () -> notifyNearby.setText(String.join("\n", lan.peer.stream()
                .filter(p -> p.warning() != null && p.nearby()).map(p -> p.who() + " (" + p.address() + "): " + p.warning()).toList()));
        lan.peer.addListener((ListChangeListener<Peer>) x -> onNearby.run());
        onNearby.run();
        notifyNearby.visibleProperty().bind(notifyNearby.textProperty().isNotEmpty());
        notifyNearby.managedProperty().bind(notifyNearby.visibleProperty());
        Button byAddress = Ui.button(I18n.t("lan.addByAddress"), "plus", () -> SharingDialogs.addAddress(app));
        byAddress.disableProperty().bind(lan.state.isNotEqualTo(LanService.State.ACTIVE));
        VBox localNetwork = new VBox(8, new HBox(12, isOn, lanState), firewallWarning, nearby, notifyNearby, new HBox(8, byAddress,
                Ui.faint(I18n.t("lan.trustNote"))));

        Label arrivalsTitle = new Label();
        arrivalsTitle.textProperty().bind(Bindings.createStringBinding(() -> I18n.t("team.incoming", c.incoming.size()), c.incoming));

        getChildren().addAll(
                Ui.panel(I18n.t("team.yourKey"), "user", you),
                panel(arrivalsTitle, "inbox", new VBox(8, arrivals, new HBox(8, paste, Ui.faint(I18n.t("paste.note"))))),
                Ui.panel(I18n.t("contacts.title"), "group", contactsBox),
                Ui.withActions(Ui.panel(I18n.t("lan.title"), "network", localNetwork), Ui.action("refresh", I18n.t("lan.refresh"), lan::refresh)));
    }

    /** Like Ui.panel, but with a title that updates. */
    private static Node panel(Label title, String icon, Node body) {
        VBox p = Ui.panel("", icon, body);
        Label t = (Label) p.getChildren().get(0);
        t.textProperty().bind(title.textProperty());
        return p;
    }

    private void refreshStatus() {
        SharingService.State s = c.status.get();
        stateTone.setText(Ui.text(s));
        stateTone.getStyleClass().setAll("label", "status", "status-tone-" + (s == SharingService.State.READY ? "ok" : "warn"));
        statusActions.getChildren().clear();
        if (s == SharingService.State.VAULT_LOCKED)
            statusActions.getChildren().add(Ui.primary(I18n.t("team.openVault"), "ok", () -> Dialogs.openVault(app.vault)));
    }
}
