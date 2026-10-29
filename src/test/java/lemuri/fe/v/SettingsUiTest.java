package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.prefs.Preferences;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.CheckBox;
import javafx.scene.control.ComboBox;
import javafx.scene.control.DialogPane;
import javafx.scene.control.ListCell;
import javafx.scene.control.ListView;
import javafx.scene.control.PasswordField;
import javafx.scene.control.TextInputControl;
import org.junit.jupiter.api.Test;
import lemuri.fe.c.MonitorController;
import lemuri.fe.v.terminal.Terminal;

/** The settings window and the vault, from the window. */
class SettingsUiTest extends UiTest {
    private void openSettings() {
        robot.clickOn(byTooltip("Settings"));
        dialog();
    }

    private void page(String name) {
        Node cell = robot.lookup(n -> n instanceof ListCell<?> c && name.equals(c.getText())).query();
        robot.clickOn(cell);
        settle();
    }

    private CheckBox check(String text) {
        return (CheckBox) robot.lookup(n -> n instanceof CheckBox c && text.equals(c.getText()) && n.getScene() != null && n.getScene().getWindow() != stage).query();
    }

    @Test
    void darkThemeAppliesAndIsRemembered() {
        boolean before = get(() -> console.getStyleClass().contains("dark"));
        openSettings();
        page("Appearance");
        assertTrue(get(() -> button("Apply").isDisabled()), "nothing to apply yet");
        robot.clickOn(check("Dark theme"));
        assertFalse(get(() -> button("Apply").isDisabled()));
        press("Apply");
        until(() -> console.getStyleClass().contains("dark") != before, "theme changed at once");
        assertEquals(!before, Preferences.userRoot().node("lemuri/ui").getBoolean("darkTheme", before));
        press("OK");
    }

    @Test
    void searchFindsTheRightPage() {
        openSettings();
        TextInputControl search = field("Search settings");
        type(search, "hosts answer");
        ListView<?> list = (ListView<?>) dialog().lookup(".settings-list");
        until(() -> list.getItems().size() == 1, "one page left");
        type(search, "zzzz-nothing");
        until(() -> list.getItems().isEmpty(), "no page");
        press("Cancel");
    }

    @Test
    void probeNeverAndNoTypedCommands() {
        openSettings();
        page("Servers");
        ComboBox<?> probe = (ComboBox<?>) dialog().lookup(".combo-box");
        on(() -> probe.getSelectionModel().select(2));
        robot.clickOn(check("Interactive mode: let Lemuri type commands into the terminal"));
        press("OK");
        assertEquals(0, MonitorController.probeMinutes());
        assertFalse(Terminal.typesCommands());
        Terminal.typesCommands(true);
    }

    @Test
    void lockAndUnlockTheVault() {
        vault.secrets.put("db-01", "pw");
        assertTrue(shows("Vault unlocked"));
        openSettings();
        page("Vault");
        robot.clickOn(robot.lookup(n -> n instanceof Button b && "Lock the vault".equals(b.getText())).queryButton());
        settle();
        assertFalse(vault.isOpen());
        press("OK");
        // The status bar follows, whoever locked it.
        until(() -> shows("Vault locked"), "status bar says locked");
        // Unlocking from the status bar: a wrong password is refused, the right one opens it.
        robot.clickOn(text("Vault locked"));
        DialogPane d = dialog();
        PasswordField pw = (PasswordField) get(() -> d.lookup(".password-field"));
        type(pw, "wrong");
        press("Unlock");
        assertFalse(vault.isOpen());
        if (!dialogOpen()) robot.clickOn(text("Vault locked"));
        PasswordField again = (PasswordField) get(() -> dialog().lookup(".password-field"));
        type(again, "vault-password");
        press("Unlock");
        until(() -> vault.isOpen() && shows("Vault unlocked"), "unlocked");
    }
}
