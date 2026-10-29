package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.File;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.List;
import javafx.scene.Node;
import javafx.scene.control.Button;
import javafx.scene.control.ListCell;
import javafx.scene.control.PasswordField;
import javafx.scene.control.RadioButton;
import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;
import lemuri.be.storage.Backup;
import lemuri.be.util.SshConfig;

/** Backup to another PC and import from ~/.ssh/config, from the window (the fake home of the tests). */
class BackupUiTest extends UiTest {
    private static final String PASSPHRASE = "a long passphrase";
    private final Path file = Path.of(System.getProperty("user.home"), "ui-backup.lemuri");

    @BeforeEach
    void chooser() throws Exception {
        Files.deleteIfExists(file);
        BackupDialogs.files = (fc, save) -> file.toFile();
    }

    @AfterEach
    void realChooser() throws Exception {
        BackupDialogs.files = (fc, save) -> save ? fc.showSaveDialog(Dialogs.main) : fc.showOpenDialog(Dialogs.main);
        Files.deleteIfExists(file);
    }

    private void settingsPage(String name) {
        robot.clickOn(byTooltip("Settings"));
        dialog();
        Node cell = robot.lookup(n -> n instanceof ListCell<?> c && name.equals(c.getText())).query();
        robot.clickOn(cell);
        settle();
    }

    private void passwords(String... values) {
        List<PasswordField> fields = get(() -> dialog().lookupAll(".password-field").stream().map(n -> (PasswordField) n).toList());
        for (int i = 0; i < values.length; i++) {
            String v = values[i];
            PasswordField f = fields.get(i);
            on(() -> f.setText(v));
        }
        settle();
    }

    private void exportAll() {
        settingsPage("Backup");
        robot.clickOn(text("Export everything…"));
        until(() -> shows("Passphrase for this file:"), "the export dialog");
        passwords("short", "short");
        assertTrue(get(() -> button("Choose where to save").isDisabled()), "a short passphrase is refused");
        passwords(PASSPHRASE, PASSPHRASE + "x");
        assertTrue(get(() -> button("Choose where to save").isDisabled()), "two different passphrases are refused");
        passwords(PASSPHRASE, PASSPHRASE);
        press("Choose where to save");
        until(() -> Files.exists(file), "the file written");
        press("OK");
        if (dialogOpen()) press("Cancel");
    }

    @Test
    void exportWritesAnEncryptedFileWithHostsAndVault() throws Exception {
        vault.secrets.put("db-01", "db-password");
        exportAll();
        byte[] bytes = Files.readAllBytes(file);
        assertFalse(new String(bytes).contains("db-password"), "encrypted");
        Backup.Contents in = Backup.open(bytes, PASSPHRASE.toCharArray());
        assertEquals(4, in.hosts().size());
        assertEquals("db-password", in.secrets().get("db-01"));
        assertEquals("rw-------", PosixFilePermissions.toString(Files.getPosixFilePermissions(file)));
    }

    @Test
    void importReplacesWhatIsHere() throws Exception {
        vault.secrets.put("db-01", "db-password");
        exportAll();
        on(() -> app.hosts.removeHost(host("homelab")));
        vault.secrets.clear();
        settingsPage("Backup");
        robot.clickOn(text("Import…"));
        passwords("wrong passphrase!");
        press("Open");
        assertTrue(dialogOpen(), "a wrong passphrase keeps the dialog open");
        passwords(PASSPHRASE);
        press("Open");
        until(() -> shows("Replace everything on this PC"), "the import dialog");
        robot.clickOn(robot.lookup(n -> n instanceof RadioButton r && r.getText().equals("Replace everything on this PC")).queryAs(RadioButton.class));
        press("Import");
        press("Replace");
        until(() -> hasHost("homelab"), "the host back");
        assertEquals("db-password", vault.password("db-01"));
        while (dialogOpen()) press(get(() -> dialog().lookupAll(".button").stream().map(n -> ((Button) n).getText()).filter(t -> t.equals("OK") || t.equals("Cancel")).findFirst().orElse("OK")));
    }

    @Test
    void importFromSshConfig() throws Exception {
        Path config = SshConfig.sshDir().resolve("config");
        Files.createDirectories(config.getParent());
        Files.writeString(config, String.join("\n",
                "Host bastion", "  HostName 198.51.100.7", "  User ops", "  Port 2222",
                "Host db-new", "  HostName 10.0.0.5", "  User app", "  ProxyJump bastion",
                "Host github.com", "  User git",
                "Host web-01", "  HostName 203.0.113.21", ""));
        try {
            robot.clickOn(text("Hosts"));
            settle();
            robot.clickOn(byTooltip("Import from ~/.ssh/config…"));
            until(() -> shows("hosts found in"), "the import dialog");
            press("Import");
            until(() -> hasHost("db-new") && hasHost("bastion"), "imported");
            assertEquals("bastion", host("db-new").jump);
            assertEquals(2222, host("bastion").port);
            assertFalse(hasHost("github.com"), "git services start unticked");
            assertEquals(1L, get(() -> app.state.hosts.stream().filter(h -> h.name.equals("web-01")).count()), "a host with the same name starts unticked");
        } finally {
            Files.deleteIfExists(config);
        }
    }
}
