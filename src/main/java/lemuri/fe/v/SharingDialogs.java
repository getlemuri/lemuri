package lemuri.fe.v;

import java.io.IOException;
import java.util.ArrayDeque;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.concurrent.CompletableFuture;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.collections.FXCollections;
import javafx.event.ActionEvent;
import javafx.geometry.Pos;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.ButtonBar;
import javafx.scene.control.ButtonType;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.Dialog;
import javafx.scene.control.Label;
import javafx.scene.control.RadioButton;
import javafx.scene.control.TextArea;
import javafx.scene.control.TextField;
import javafx.scene.control.ToggleGroup;
import javafx.scene.input.Clipboard;
import javafx.scene.input.ClipboardContent;
import javafx.scene.layout.HBox;
import javafx.scene.layout.Priority;
import javafx.scene.layout.VBox;
import javafx.util.StringConverter;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Host;
import lemuri.be.dto.Parcel;
import lemuri.be.dto.Peer;
import lemuri.be.dto.Request;
import lemuri.be.dto.TrustedPeer;
import lemuri.be.service.SharingService;
import lemuri.be.sharing.Envelope;
import lemuri.be.sharing.TextEnvelope;
import lemuri.be.util.ContactNames;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.fe.c.AppContext;
import lemuri.fe.c.LanController;
import lemuri.fe.c.SharingController;
import lemuri.fe.m.AppState;
import lemuri.fe.m.Selection;
import lemuri.fe.m.Task;
import lemuri.fe.v.sections.Contacts;

/** Dialogs for the exchange with contacts: sending a host, pasting and accepting one, adding a contact, pairing. */
public final class SharingDialogs {
    private SharingDialogs() {}

    /** Shows in the log how an operation went. */
    public static void outcome(CompletableFuture<?> f, String done) {
        f.whenComplete((v, e) -> Platform.runLater(() -> {
            if (e == null) return;
            Throwable c = e.getCause() != null ? e.getCause() : e;
            Dialogs.warning(I18n.t("sharing.title"), I18n.t("sharing.failed", done, c.getMessage()));
        }));
    }

    /** Sharing needs the vault (the keys are there) and at least one contact: if not, it says why and goes to the Team page. */
    private static boolean ready(AppContext app) {
        if (app.sharing.status.get() != SharingService.State.READY && !Dialogs.openVault(app.vault)) return false;
        if (!app.sharing.contacts().isEmpty()) return true;
        Dialogs.warning(I18n.t("sharing.title"), I18n.t("send.noContacts"));
        app.state.select(new Selection.Contacts(), null);
        return false;
    }

    // ── Sending ──────────────────────────────────────────────────────────────

    public static void send(AppContext app, Host h) {
        if (!ready(app)) return;
        SharingController c = app.sharing;
        List<CheckBox> choices = new ArrayList<>();
        VBox recipients = new VBox(6);
        for (TrustedPeer m : c.contacts()) {
            boolean onLan = c.lan.reachable(m.key()) != null;
            CheckBox b = new CheckBox(m.name() + "  · " + I18n.t(onLan ? "send.localNetwork" : "send.asText"));
            b.setUserData(m);
            recipients.getChildren().add(new HBox(10, b, Ui.faint(I18n.t("send.fingerprint", Envelope.fingerprint(m.key())))));
            choices.add(b);
        }

        String saved = app.vault.password(h.id);
        CheckBox withPassword = new CheckBox(I18n.t("send.includePassword"));
        withPassword.setSelected(saved != null);
        withPassword.setDisable(saved == null);
        if (saved == null) withPassword.setText(I18n.t("send.noPassword"));
        TextField message = Ui.limit(new TextField(), Ui.MAX_MESSAGE);
        message.setPromptText(I18n.t("send.message.prompt"));

        VBox body = new VBox(10,
                new Label(I18n.t("send.host", h.name, h.destination())),
                Ui.faint(I18n.t("send.to")), recipients, withPassword,
                Ui.faint(I18n.t("team.col.message")), message);
        if (h.authMethod == Host.AuthMethod.KEY || h.authMethod == Host.AuthMethod.AGENT)
            body.getChildren().add(note(I18n.t("send.keyNote")));
        body.getChildren().add(note(I18n.t("send.cryptoNote")));
        body.setPrefWidth(520);

        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("send.title"));
        ButtonType ok = new ButtonType(I18n.t("hosts.send"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().setContent(body);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        Runnable check = () -> bOk.setDisable(choices.stream().noneMatch(CheckBox::isSelected));
        choices.forEach(b -> b.selectedProperty().addListener(o -> check.run()));
        check.run();
        if (d.showAndWait().filter(ok::equals).isEmpty()) return;

        List<TrustedPeer> a = choices.stream().filter(CheckBox::isSelected).map(b -> (TrustedPeer) b.getUserData()).toList();
        String text = message.getText().isBlank() ? null : message.getText().trim();
        String names = String.join(", ", a.stream().map(TrustedPeer::name).toList());
        Task t = app.state.task(h.name, I18n.t("task.sendingTo", names));
        c.send(h, a, withPassword.isSelected() ? saved : null, text).whenComplete((out, e) -> Platform.runLater(() -> {
            t.finish(e == null ? Task.Outcome.OK : Task.Outcome.ERROR, e == null ? null : (e.getCause() != null ? e.getCause() : e).getMessage());
            if (e == null) for (SharingService.Outgoing o : out) if (o.text() != null) showEnvelope(h, o);
        }));
    }

    /** The envelope for a contact whose PC is not on the local network: to copy and pass on by any channel. */
    private static void showEnvelope(Host h, SharingService.Outgoing o) {
        TextArea text = new TextArea(o.text());
        text.setEditable(false);
        text.setWrapText(false);
        text.setPrefRowCount(10);
        text.getStyleClass().add("code-area");
        VBox body = new VBox(10, new Label(I18n.t("envelope.for", h.name, o.to().name())), text, note(I18n.t("envelope.note", o.to().name())));
        body.setPrefWidth(560);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("envelope.title"));
        ButtonType copy = new ButtonType(I18n.t("envelope.copy"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CLOSE, copy);
        d.getDialogPane().setContent(body);
        Node bCopy = d.getDialogPane().lookupButton(copy);
        bCopy.getStyleClass().add("btn-primary");
        // Copying does not close the window: the text stays there until it has been pasted somewhere.
        bCopy.addEventFilter(ActionEvent.ACTION, e -> {
            copy(o.text());
            ((Button) bCopy).setText(I18n.t("envelope.copied"));
            e.consume();
        });
        d.showAndWait();
    }

    private static void copy(String text) {
        ClipboardContent c = new ClipboardContent();
        c.putString(text);
        Clipboard.getSystemClipboard().setContent(c);
    }

    /** Pasting an envelope received by email or chat: if it opens, it is offered like one arrived over the network. */
    public static void paste(AppContext app) {
        if (app.sharing.status.get() != SharingService.State.READY && !Dialogs.openVault(app.vault)) return;
        TextArea text = Ui.limit(new TextArea(), TextEnvelope.MAX_TEXT);
        text.setPromptText(TextEnvelope.BEGIN + "\n…\n" + TextEnvelope.END);
        text.setPrefRowCount(10);
        text.getStyleClass().add("code-area");
        String clip = Clipboard.getSystemClipboard().getString();
        if (clip != null && clip.contains(TextEnvelope.BEGIN)) text.setText(clip);
        Label error = new Label();
        error.getStyleClass().add("warn-text");
        error.setWrapText(true);
        VBox body = new VBox(10, Ui.faint(I18n.t("paste.label")), text, error);
        body.setPrefWidth(560);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("paste.title"));
        ButtonType ok = new ButtonType(I18n.t("paste.open"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().setContent(body);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        bOk.disableProperty().bind(text.textProperty().isEmpty());
        Delivery[] opened = new Delivery[1];
        // A wrong envelope keeps the window open, with the reason.
        bOk.addEventFilter(ActionEvent.ACTION, e -> {
            try {
                opened[0] = app.sharing.open(text.getText());
            } catch (IllegalArgumentException | IllegalStateException x) {
                // not an envelope, not for us, from a stranger, already decided, or the vault locked
                error.setText(x.getMessage());
                e.consume();
            }
        });
        if (d.showAndWait().filter(ok::equals).isEmpty() || opened[0] == null) return;
        accept(app, opened[0], true);
    }

    // ── Contacts ─────────────────────────────────────────────────────────────

    /** This user's key card: to send to whoever wants to add them as a contact. */
    public static void myKey(AppContext app) {
        if (app.sharing.status.get() != SharingService.State.READY && !Dialogs.openVault(app.vault)) return;
        String card = app.sharing.myKeyCard();
        if (card == null) return;
        TextField line = new TextField(card);
        line.setEditable(false);
        line.getStyleClass().add("code-area");
        Label fingerprint = new Label(I18n.t("team.yourFingerprint", app.sharing.myFingerprint()));
        fingerprint.getStyleClass().add("pairing-code-small");
        VBox body = new VBox(10, Ui.faint(I18n.t("mykey.label")), line, fingerprint, note(I18n.t("mykey.note")));
        body.setPrefWidth(560);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("mykey.title"));
        ButtonType copy = new ButtonType(I18n.t("envelope.copy"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CLOSE, copy);
        d.getDialogPane().setContent(body);
        Node bCopy = d.getDialogPane().lookupButton(copy);
        bCopy.getStyleClass().add("btn-primary");
        bCopy.addEventFilter(ActionEvent.ACTION, e -> {
            copy(card);
            ((Button) bCopy).setText(I18n.t("envelope.copied"));
            e.consume();
        });
        d.showAndWait();
    }

    /**
     * Adds a contact from the key card they sent. The card may have travelled through anyone's hands:
     * the fingerprint must be compared with them by voice or in person before it is saved.
     */
    public static void addContact(AppContext app) {
        if (app.sharing.status.get() != SharingService.State.READY && !Dialogs.openVault(app.vault)) return;
        TextField card = Ui.limit(new TextField(), Ui.MAX_CARD);
        card.setPromptText("LEMURI-KEY 1 …");
        String clip = Clipboard.getSystemClipboard().getString();
        if (clip != null && TextEnvelope.parseKey(clip) != null) card.setText(clip.strip());
        TextField name = Ui.limit(new TextField(), Ui.MAX_NAME);
        name.setPromptText(I18n.t("contact.name.prompt"));
        Label fingerprint = new Label("");
        fingerprint.getStyleClass().add("pairing-code-small");
        CheckBox compared = new CheckBox(I18n.t("contact.compared"));
        Label error = new Label();
        error.getStyleClass().add("warn-text");
        error.setWrapText(true);
        Runnable read = () -> {
            TextEnvelope.KeyCard k = TextEnvelope.parseKey(card.getText());
            fingerprint.setText(k != null ? I18n.t("contact.fingerprint", Envelope.fingerprint(k.key())) : "");
            if (k != null && name.getText().isBlank()) name.setText(k.name());
            compared.setSelected(false);
        };
        card.textProperty().addListener(o -> read.run());
        read.run();
        VBox body = new VBox(10, Ui.faint(I18n.t("contact.card")), card, Ui.faint(I18n.t("contact.name")), name, fingerprint,
                note(I18n.t("contact.compareNote")), compared, error);
        body.setPrefWidth(560);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("contact.title"));
        ButtonType ok = new ButtonType(I18n.t("common.add"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().setContent(body);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        bOk.disableProperty().bind(compared.selectedProperty().not().or(fingerprint.textProperty().isEmpty()));
        bOk.addEventFilter(ActionEvent.ACTION, e -> {
            try {
                app.sharing.addContact(card.getText(), name.getText());
                app.state.task(name.getText().strip(), I18n.t("task.contactAdded")).finish(Task.Outcome.OK, null);
            } catch (IOException | IllegalArgumentException | IllegalStateException x) {
                // not a key, already there, name taken, or the vault locked or not writable
                error.setText(x.getMessage());
                e.consume();
            }
        });
        d.showAndWait();
    }

    // ── Receiving ────────────────────────────────────────────────────────────

    /** Arrivals waiting to be offered: one window at a time, the others wait their turn. */
    private static final ArrayDeque<Delivery> toPropose = new ArrayDeque<>();
    private static boolean proposing;

    /**
     * As soon as a host arrives it is offered: Accept, Reject or Later. With
     * "Later" it stays in Team → Incoming and is not offered again.
     */
    public static void propose(AppContext app, Delivery i) {
        toPropose.add(i);
        if (proposing) return;
        proposing = true;
        // After the refresh in progress: the window does not open inside the round that brought the arrival.
        Platform.runLater(() -> {
            try {
                for (Delivery x; (x = toPropose.poll()) != null; ) {
                    String id = x.id();
                    // Already handled (by another window or from Incoming) while it was waiting.
                    if (app.sharing.incoming.stream().noneMatch(a -> a.id().equals(id))) continue;
                    accept(app, x, true);
                }
            } finally {
                proposing = false;
            }
        });
    }

    public static void accept(AppContext app, Delivery i) {
        accept(app, i, false);
    }

    /** {@code justArrived}: opened by itself on arrival, and then "Cancel" becomes "Later". */
    private static void accept(AppContext app, Delivery i, boolean justArrived) {
        Parcel p = i.content();
        if (p == null) return;
        SharingController c = app.sharing;
        Host created = p.asHost();
        // The twin gets the password that arrived, so the address must really be the same: equalsIgnoreCase would
        // also match look-alikes such as "ſerver" and "server" (the long s becomes S in upper case).
        String address = created.address.toLowerCase(Locale.ROOT);
        Host twin = app.state.hosts.stream().filter(x -> x.address.toLowerCase(Locale.ROOT).equals(address) && x.port == created.port
                && x.user.equals(created.user)).findFirst().orElse(null);
        String senderFingerprint = i.fromKey() != null ? Envelope.fingerprint(i.fromKey()) : "?";

        // The fingerprint is what the user compares: the line wraps instead of being cut with "...".
        Label sender = new Label(I18n.t("accept.from", i.from(), senderFingerprint));
        sender.setWrapText(true);
        VBox details = new VBox(4,
                sender,
                new Label(I18n.t("accept.host", p.name, p.user + "@" + p.address + ":" + p.port)),
                new Label(I18n.t("accept.access", switch (created.authMethod) {
                    case PASSWORD -> I18n.t("summary.auth.password");
                    case KEY -> I18n.t("accept.access.key");
                    case AGENT -> I18n.t("accept.access.agent");
                }) + (p.password != null ? "  · " + I18n.t("accept.passwordIncluded") : "")));
        if (p.message != null) details.getChildren().add(new Label(I18n.t("accept.message", p.message)));

        ToggleGroup where = new ToggleGroup();
        RadioButton aside = new RadioButton(I18n.t("accept.keepApart"));
        RadioButton inFolder = new RadioButton(I18n.t("accept.inFolder"));
        aside.setToggleGroup(where);
        inFolder.setToggleGroup(where);
        List<String> folders = new ArrayList<>();
        folders.add("");
        folders.addAll(app.state.folders());
        ComboBox<String> folder = new ComboBox<>(FXCollections.observableArrayList(folders));
        folder.setValue("");
        folder.setConverter(new StringConverter<>() {
            @Override public String toString(String s) { return s == null ? "" : Folders.label(s); }

            @Override public String fromString(String s) { return s; }
        });
        folder.disableProperty().bind(inFolder.selectedProperty().not());
        VBox choices = new VBox(8, aside, new HBox(8, inFolder, folder));
        RadioButton refresh = null;
        if (twin != null) {
            refresh = new RadioButton(p.password == null ? I18n.t("accept.updateNone", twin.name) : I18n.t("accept.update", twin.name));
            refresh.setToggleGroup(where);
            refresh.setDisable(p.password == null);
            choices.getChildren().add(0, refresh);
        }
        // Never overwriting your own password as a preselected choice: it must be chosen on purpose.
        aside.setSelected(true);

        VBox body = new VBox(12, details, Ui.faint(I18n.t("accept.where")), choices);
        body.setPrefWidth(520);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, justArrived ? I18n.t("accept.title.new", i.from()) : I18n.t("accept.title", i.from()));
        ButtonType ok = new ButtonType(I18n.t("accept.button"), ButtonBar.ButtonData.OK_DONE);
        ButtonType no = new ButtonType(I18n.t("team.reject"), ButtonBar.ButtonData.LEFT);
        ButtonType after = justArrived ? new ButtonType(I18n.t("accept.later"), ButtonBar.ButtonData.CANCEL_CLOSE) : ButtonType.CANCEL;
        d.getDialogPane().getButtonTypes().setAll(no, after, ok);
        d.getDialogPane().setContent(body);
        d.getDialogPane().lookupButton(ok).getStyleClass().add("btn-primary");
        d.getDialogPane().lookupButton(no).getStyleClass().add("btn-danger");
        ButtonType chosen = d.showAndWait().orElse(after);
        if (chosen == no) {
            remove(c, i);
            return;
        }
        if (chosen != ok) return;
        // The vault may have been locked while the dialog was open: the password needs it.
        if (p.password != null && !app.vault.isOpen() && !Dialogs.openVault(app.vault)) return;

        try {
            Host saved;
            if (refresh != null && refresh.isSelected()) {
                saved = twin;
            } else {
                created.id = AppState.uid();
                created.fromContact = i.from();
                created.received = aside.isSelected();
                created.group = aside.isSelected() ? "" : Folders.normalize(folder.getValue());
                if (app.state.hosts.stream().anyMatch(x -> x.name.equalsIgnoreCase(created.name))) created.name = created.name + "-" + i.from().split("@")[0];
                app.hosts.saveHost(created);
                saved = created;
            }
            if (p.password != null) app.vault.remember(saved.id, p.password);
            app.state.task(saved.name, I18n.t("task.acceptedFrom", i.from())).finish(Task.Outcome.OK, null);
        } catch (IOException | IllegalStateException e) {
            Dialogs.warning(I18n.t("accept.button"), I18n.t("accept.noPassword", e.getMessage()));
        }
        remove(c, i);
    }

    /** Removes a handled delivery, telling the user if the vault could not remember it. */
    public static void remove(SharingController c, Delivery i) {
        if (!c.remove(i)) Dialogs.warning(I18n.t("team.notRemembered.title"), I18n.t("team.notRemembered", i.from()));
    }

    // ── Local network ────────────────────────────────────────────────────────

    /**
     * Pairing requested from here: the code is shown and we wait for the other
     * PC to accept; then here too the user confirms that the code was the same.
     */
    public static void pair(AppContext app, Peer p) {
        LanController l = app.sharing.lan;
        Label number = new Label("··· ···");
        number.getStyleClass().add("pairing-code");
        Label wait = new Label(I18n.t("link.exchanging", p.who()));
        TextField name = Ui.limit(new TextField(), Ui.MAX_NAME);
        BooleanProperty nameOk = new SimpleBooleanProperty();
        VBox body = new VBox(12,
                new Label(I18n.t("link.with", p.who(), p.address(), p.fingerprint())),
                Ui.faint(I18n.t("link.checkCode")),
                number, wait,
                contactName(l, p, name, nameOk),
                note(I18n.t("link.different")));
        body.setPrefWidth(480);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("link.title"));
        ButtonType ok = new ButtonType(I18n.t("link.sameCode"), ButtonBar.ButtonData.OK_DONE);
        d.getDialogPane().getButtonTypes().setAll(ButtonType.CANCEL, ok);
        d.getDialogPane().setContent(body);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        BooleanProperty accepted = new SimpleBooleanProperty();
        bOk.disableProperty().bind(accepted.not().or(nameOk.not()));
        l.pair(p, code -> {
            number.setText(code);
            wait.setText(I18n.t("link.waiting", p.who()));
        }).whenComplete((yes, e) -> Platform.runLater(() -> {
            if (e != null) wait.setText(I18n.t("link.failed", (e.getCause() != null ? e.getCause() : e).getMessage()));
            else if (!yes) wait.setText(I18n.t("link.rejected", p.who()));
            else {
                wait.setText(I18n.t("link.accepted", p.who()));
                accepted.set(true);
            }
        }));
        if (d.showAndWait().filter(ok::equals).isEmpty()) return;
        try {
            l.trusted(p.key(), name.getText());
            app.state.task(name.getText(), I18n.t("task.linked")).finish(Task.Outcome.OK, null);
        } catch (IOException | IllegalArgumentException | IllegalStateException e) {
            Dialogs.warning(I18n.t("link.short"), I18n.t("link.cannotSave", e.getMessage()));
        }
    }

    /**
     * Another PC asks to pair: accept only if the code is the same on both screens.
     * The name to give the contact if accepted, null if refused.
     */
    public static String pairingRequest(LanController l, Request r) {
        Peer p = r.peer();
        Label number = new Label(r.code());
        number.getStyleClass().add("pairing-code");
        TextField name = Ui.limit(new TextField(), Ui.MAX_NAME);
        BooleanProperty nameOk = new SimpleBooleanProperty();
        VBox body = new VBox(12,
                new Label(I18n.t("link.request.wants", p.who())),
                Ui.faint(I18n.t("link.request.from", p.address(), p.fingerprint())
                        + (p.email() != null && !p.email().isBlank() ? " " + I18n.t("link.request.unverified") : "")),
                Ui.faint(I18n.t("link.request.same")),
                number,
                contactName(l, p, name, nameOk),
                note(I18n.t("link.request.note")));
        body.setPrefWidth(480);
        Dialog<ButtonType> d = new Dialog<>();
        Dialogs.prepare(d, I18n.t("link.request.title"));
        ButtonType ok = new ButtonType(I18n.t("link.sameCode"), ButtonBar.ButtonData.OK_DONE);
        ButtonType no = new ButtonType(I18n.t("team.reject"), ButtonBar.ButtonData.CANCEL_CLOSE);
        d.getDialogPane().getButtonTypes().setAll(no, ok);
        d.getDialogPane().setContent(body);
        Node bOk = d.getDialogPane().lookupButton(ok);
        bOk.getStyleClass().add("btn-primary");
        bOk.disableProperty().bind(nameOk.not());
        return d.showAndWait().filter(ok::equals).isPresent() ? name.getText() : null;
    }

    /**
     * The name the contact will have, suggested from what the other PC announces. Deliveries show it as "from",
     * and anyone on the network can announce any name: if it looks like a contact with another key, the user
     * is told so and must choose another one ({@code ok} stays false until then).
     */
    private static Node contactName(LanController l, Peer p, TextField field, BooleanProperty ok) {
        field.setText(ContactNames.clean(p.who()));
        Label problem = new Label();
        problem.getStyleClass().add("warn-text");
        problem.setWrapText(true);
        problem.managedProperty().bind(problem.visibleProperty());
        Runnable check = () -> {
            String n = ContactNames.clean(field.getText());
            String wrong = ContactNames.problem(n);
            String other = wrong != null ? null : l.nameTakenBy(n, p.key());
            String otherKey = other == null ? null : l.trusted.stream().filter(t -> t.name().equals(other)).map(TrustedPeer::key).findFirst().orElse(null);
            problem.setText(wrong != null ? I18n.t(wrong)
                    : other != null ? I18n.t("link.nameTaken", other, otherKey != null ? Envelope.fingerprint(otherKey) : "?") : "");
            problem.setVisible(!problem.getText().isEmpty());
            ok.set(wrong == null && other == null);
        };
        field.textProperty().addListener((o, a, b) -> check.run());
        check.run();
        HBox row = new HBox(10, new Label(I18n.t("link.contactName")), field);
        row.setAlignment(Pos.CENTER_LEFT);
        HBox.setHgrow(field, Priority.ALWAYS);
        return new VBox(6, row, problem);
    }

    /** Adds a PC by address, when its announcements do not arrive (firewall, different networks, VPN). */
    public static void addAddress(AppContext app) {
        Dialogs.name(I18n.t("lan.add.title"), I18n.t("lan.add.label"), "", I18n.t("common.add"),
                x -> x.isBlank() || x.contains(" ") ? I18n.t("lan.add.enter") : null).ifPresent(i ->
                app.sharing.lan.addAddress(i.trim()).whenComplete((p, e) -> Platform.runLater(() -> {
                    if (e != null) Dialogs.warning(I18n.t("lan.add.short"), (e.getCause() != null ? e.getCause() : e).getMessage());
                    else if (!app.sharing.lan.trusted(p.key())
                            && Dialogs.confirm(I18n.t("lan.add.short"), I18n.t("lan.add.found", p.who(), p.address()), I18n.t("lan.link")))
                        pair(app, p);
                })));
    }

    private static Label note(String text) {
        Label l = Ui.faint(text);
        l.setWrapText(true);
        return l;
    }
}
