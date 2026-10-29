package lemuri.be.util;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.prefs.BackingStoreException;
import java.util.prefs.Preferences;

/**
 * The first versions saved preferences and some files under Italian names. At startup, before anything
 * reads them, they are moved to the English names once; afterwards there is nothing left to do. The vault
 * and the JSON files are migrated where they are read (see FileVault and the Gson field names).
 */
public final class LegacyNames {
    private static final String[][] PREFERENCES = {
        {"lemuri/ui", "lingua", "lemuri/ui", "language"},
        // The modern theme is gone: its choice, under both names, is dropped.
        {"lemuri/ui", "moderno", null, null},
        {"lemuri/ui", "modernTheme", null, null},
        {"lemuri/ui", "scuro", "lemuri/ui", "darkTheme"},
        {"lemuri/ui", "risorse", "lemuri/ui", "hostColumn"},
        {"lemuri/ui", "cornice-personalizzata", "lemuri/ui", "customFrame"},
        {"lemuri/ui", "layoutVs", null, null},
        {"lemuri/condivisione", "attiva", "lemuri/sharing", "lanOn"},
        {"lemuri/condivisione", "indirizzi", "lemuri/sharing", "lanAddresses"},
        {"lemuri/host", "daDimenticare", "lemuri/host", "passwordsToForget"},
    };

    private LegacyNames() {}

    /** The app was called VpsMan: its data folder and preference tree under the old name. */
    static final String OLD_NAME = "vpsmanager";

    public static void migrate() {
        Path home = Path.of(System.getProperty("user.home"));
        folder(home.resolve("." + OLD_NAME), home.resolve(".lemuri"));
        tree(Preferences.userRoot(), OLD_NAME, "lemuri");
        preferences(Preferences.userRoot());
        files(home.resolve(".lemuri"));
    }

    /** The old data folder takes the new name, if there is no new one yet: hosts, vault and logs stay as they are. */
    static void folder(Path old, Path now) {
        try {
            if (Files.isDirectory(old) && !Files.exists(now)) Files.move(old, now);
        } catch (IOException e) {
            // the old one stays: the app starts empty and the user can move it by hand
        }
    }

    /** Copies the old preference tree under the new name (values already there win) and removes the old one. */
    static void tree(Preferences root, String old, String now) {
        try {
            if (!root.nodeExists(old)) return;
            copy(root.node(old), root.node(now));
            root.node(old).removeNode();
            root.flush();
        } catch (BackingStoreException | IllegalStateException e) {
            // the preferences cannot be read or written: the defaults apply
        }
    }

    private static void copy(Preferences from, Preferences to) throws BackingStoreException {
        for (String k : from.keys()) if (to.get(k, null) == null) to.put(k, from.get(k, null));
        for (String c : from.childrenNames()) copy(from.node(c), to.node(c));
    }

    /** Moves every old key to its new name (a value already saved under the new name wins); a null target drops it. */
    static void preferences(Preferences root) {
        try {
            for (String[] m : PREFERENCES) {
                if (!root.nodeExists(m[0])) continue;
                Preferences from = root.node(m[0]);
                String v = from.get(m[1], null);
                if (v == null) continue;
                if (m[2] != null) {
                    Preferences to = root.node(m[2]);
                    if (to.get(m[3], null) == null) to.put(m[3], v);
                }
                from.remove(m[1]);
            }
            if (root.nodeExists("lemuri/condivisione")) {
                Preferences old = root.node("lemuri/condivisione");
                if (old.keys().length == 0 && old.childrenNames().length == 0) old.removeNode();
            }
            root.flush();
        } catch (BackingStoreException | IllegalStateException e) {
            // the preferences cannot be read or written: the defaults apply
        }
    }

    /** Files of the first versions that are no longer used: the single-instance lock and the tray icon folder. */
    static void files(Path dir) {
        try {
            Files.deleteIfExists(dir.resolve("istanza.porta"));
            Files.deleteIfExists(dir.resolve("istanza.lock"));
            Path icons = dir.resolve("icone");
            if (Files.isDirectory(icons)) {
                try (var l = Files.list(icons)) {
                    for (Path p : l.toList()) Files.deleteIfExists(p);
                }
                Files.deleteIfExists(icons);
            }
        } catch (IOException e) {
            // they stay there, harmless
        }
    }
}
