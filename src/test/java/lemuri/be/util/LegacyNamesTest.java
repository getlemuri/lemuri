package lemuri.be.util;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;

import java.nio.file.Files;
import java.nio.file.Path;
import java.util.prefs.Preferences;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/** Preferences saved by the first versions under Italian names move to the English ones, once. */
class LegacyNamesTest {
    @Test
    void oldPreferencesMoveToTheNewNames() throws Exception {
        Preferences root = Preferences.userRoot().node("lemuri-test-legacy");
        try {
            root.node("lemuri/ui").put("lingua", "it");
            root.node("lemuri/ui").putBoolean("scuro", true);
            root.node("lemuri/ui").put("darkTheme", "false");
            root.node("lemuri/ui").put("layoutVs", "true");
            root.node("lemuri/ui").put("modernTheme", "false");
            root.node("lemuri/ui").put("moderno", "true");
            root.node("lemuri/condivisione").putBoolean("attiva", true);
            root.node("lemuri/condivisione").put("indirizzi", "10.0.0.5");

            LegacyNames.preferences(root);

            Preferences ui = root.node("lemuri/ui"), sharing = root.node("lemuri/sharing");
            assertEquals("it", ui.get("language", null));
            // A value already saved under the new name wins.
            assertEquals("false", ui.get("darkTheme", null));
            assertNull(ui.get("scuro", null));
            assertNull(ui.get("modernTheme", null));
            assertNull(ui.get("moderno", null));
            assertNull(ui.get("layoutVs", null));
            assertEquals(true, sharing.getBoolean("lanOn", false));
            assertEquals("10.0.0.5", sharing.get("lanAddresses", null));
            assertFalse(root.nodeExists("lemuri/condivisione"));
        } finally {
            root.removeNode();
        }
    }

    /** The app was called VpsMan: its preferences and data folder move to the new name, without overwriting. */
    @Test
    void theOldNameMovesToTheNewOne(@TempDir Path home) throws Exception {
        Preferences root = Preferences.userRoot().node("lemuri-test-rename");
        try {
            root.node(LegacyNames.OLD_NAME + "/ui").put("darkTheme", "true");
            root.node(LegacyNames.OLD_NAME + "/ui").put("language", "it");
            root.node("lemuri/ui").put("language", "en");
            LegacyNames.tree(root, LegacyNames.OLD_NAME, "lemuri");
            assertEquals("true", root.node("lemuri/ui").get("darkTheme", null));
            assertEquals("en", root.node("lemuri/ui").get("language", null), "a value already there wins");
            assertFalse(root.nodeExists(LegacyNames.OLD_NAME));
        } finally {
            root.removeNode();
        }
        Path old = Files.createDirectories(home.resolve(".old"));
        Files.writeString(old.resolve("hosts.json"), "{}");
        LegacyNames.folder(old, home.resolve(".lemuri"));
        assertEquals("{}", Files.readString(home.resolve(".lemuri/hosts.json")));
        assertFalse(Files.exists(old));
    }
}
