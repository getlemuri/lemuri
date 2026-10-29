package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import javafx.scene.control.CheckBox;
import javafx.scene.control.TextArea;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Host;
import lemuri.be.dto.TrustedPeer;
import lemuri.be.sharing.Sharing;

/** Contacts and hosts sent as text, from the window; Sam, the other person, is a second sharing engine. */
class SharingUiTest extends UiTest {
    private MemoryVault samVault;
    private Sharing sam;

    @BeforeEach
    void sam() {
        samVault = new MemoryVault();
        sam = new Sharing(samVault);
        sam.restart();
        until(() -> sam.myKeyCard() != null, "Sam's keys");
    }

    @AfterEach
    void noSam() {
        sam.stop();
    }

    private void team() {
        robot.clickOn(byTooltip("Team"));
        settle();
    }

    /** Sam becomes our contact through the dialog, with the key line Sam sent us. */
    private void addSamAsContact() {
        team();
        robot.clickOn(text("Add a contact"));
        on(() -> field("LEMURI-KEY 1 …").setText(sam.myKeyCard()));
        type(field("how you will see them: mario, Mario Rossi…"), "sam");
        robot.clickOn(robot.lookup(n -> n instanceof CheckBox c && c.getText().startsWith("I compared")).queryAs(CheckBox.class));
        press("Add");
        until(() -> sharing.lan().contacts().stream().anyMatch(c -> c.name().equals("sam")), "sam among the contacts");
    }

    @Test
    void addAContactWithTheirKeyLine() {
        addSamAsContact();
        until(() -> shows("sam"), "sam in the contacts table");
    }

    @Test
    void aContactMustBeComparedFirst() {
        team();
        robot.clickOn(text("Add a contact"));
        on(() -> field("LEMURI-KEY 1 …").setText(sam.myKeyCard()));
        type(field("how you will see them: mario, Mario Rossi…"), "sam");
        assertTrue(get(() -> button("Add").isDisabled()), "not before ticking that the fingerprint was compared");
        press("Cancel");
        assertTrue(sharing.lan().contacts().isEmpty());
    }

    @Test
    void pasteAndAcceptAHostFromSam() throws Exception {
        addSamAsContact();
        sam.addContact(sharing.myKeyCard(), "dome");
        TrustedPeer me = sam.lan().contacts().get(0);
        Host db = host("db-sam", "", "198.51.100.30");
        String text = sam.send(db, List.of(me), "sam-password", "the new database").get().get(0).text();

        robot.clickOn(text("Paste a received host"));
        on(() -> ((TextArea) dialog().lookup(".text-area")).setText("Hi, here it is:\n" + text + "\nbye"));
        press("Open");
        until(() -> shows("New host from sam"), "the acceptance dialog");
        assertTrue(shows("db-sam"));
        assertTrue(shows("the new database"));
        press("Accept");
        until(() -> hasHost("db-sam"), "the host in the list");
        assertEquals("198.51.100.30", host("db-sam").address);
        assertEquals("sam-password", vault.password(host("db-sam").id));

        // The same text again: already decided, refused.
        robot.clickOn(text("Paste a received host"));
        on(() -> ((TextArea) dialog().lookup(".text-area")).setText(text));
        press("Open");
        assertTrue(dialogOpen(), "the dialog stays open with the reason");
        press("Cancel");
        assertEquals(1, get(() -> app.state.hosts.stream().filter(h -> h.name.equals("db-sam")).count()));
    }

    @Test
    void aTextFromAStrangerOrChangedIsRefused() throws Exception {
        // Sam knows us, we do not know Sam.
        sam.addContact(sharing.myKeyCard(), "dome");
        String text = sam.send(host("x", "", "198.51.100.31"), List.of(sam.lan().contacts().get(0)), null, null).get().get(0).text();
        team();
        robot.clickOn(text("Paste a received host"));
        on(() -> ((TextArea) dialog().lookup(".text-area")).setText(text));
        press("Open");
        until(() -> shows("not among your contacts"), "refused: unknown sender");
        on(() -> ((TextArea) dialog().lookup(".text-area")).setText("hello"));
        press("Open");
        until(() -> shows("not a Lemuri host"), "refused: not an envelope");
        press("Cancel");
        assertFalse(hasHost("x"));
    }

    @Test
    void sendAHostToSamAsText() throws Exception {
        addSamAsContact();
        sam.addContact(sharing.myKeyCard(), "dome");
        vault.secrets.put("db-01", "db-password");
        menu("db-01", "Send to team…");
        robot.clickOn(robot.lookup(n -> n instanceof CheckBox c && c.getText().startsWith("sam")).queryAs(CheckBox.class));
        press("Send");
        until(() -> dialogOpen() && dialog().lookup(".text-area") != null, "the encrypted text");
        String text = get(() -> ((TextArea) dialog().lookup(".text-area")).getText());
        press("Copy");
        Delivery d = sam.open(text);
        assertEquals("dome", d.from());
        assertEquals("db-01", d.content().name);
        assertEquals("db-password", d.content().password, "the saved password goes along, ticked by default");
    }
}
