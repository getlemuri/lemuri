package lemuri.fe.v.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.Map;
import org.junit.jupiter.api.Test;
import lemuri.be.terminal.FileCheck;

/** Interactive mode: where a double-clicked name is, and how it is typed. */
class DoubleClickTest {
    @Test
    void theFolderGivenToLsIsFound() {
        assertNull(Terminal.lsFolder("ls"));
        assertNull(Terminal.lsFolder("ls -la"));
        assertNull(Terminal.lsFolder(""));
        assertEquals("/var/log", Terminal.lsFolder("ls -l /var/log"));
        assertEquals("backups", Terminal.lsFolder("ll backups"));
        assertEquals("my files", Terminal.lsFolder("ls 'my files'"), "quotes are removed");
        assertNull(Terminal.lsFolder("ls a b"), "two folders: cannot tell");
    }

    /** After "ls /var/log" the name is looked for there first, typed with its full path; then in the shell's folder. */
    @Test
    void theNameIsLookedForInTheListedFolderThenInTheShellOne() {
        Map<String, FileCheck.Kind> server = Map.of("/var/log/nginx", FileCheck.Kind.FOLDER, "/root/notes.txt", FileCheck.Kind.TEXT,
                "/root/backups/db.sql", FileCheck.Kind.LARGE);
        DoubleClick.Found nginx = DoubleClick.find("/root", "/var/log", "nginx", () -> "/root", p -> server.getOrDefault(p, FileCheck.Kind.MISSING));
        assertEquals(FileCheck.Kind.FOLDER, nginx.kind());
        assertEquals("/var/log/nginx", nginx.typed());
        DoubleClick.Found notes = DoubleClick.find("/root", null, "notes.txt", () -> "/root", p -> server.getOrDefault(p, FileCheck.Kind.MISSING));
        assertEquals("notes.txt", notes.typed(), "in the shell's folder the name is typed as it is");
        DoubleClick.Found dump = DoubleClick.find("/srv", "~/backups", "db.sql", () -> "/root", p -> server.getOrDefault(p, FileCheck.Kind.MISSING));
        assertEquals(FileCheck.Kind.LARGE, dump.kind(), "~ is the home");
        DoubleClick.Found gone = DoubleClick.find("/root", null, "gone", () -> "/root", p -> FileCheck.Kind.MISSING);
        assertEquals(FileCheck.Kind.MISSING, gone.kind());
    }

    @Test
    void namesAreTypedSafely() {
        assertEquals("./-rf", DoubleClick.places("/root", null, "-rf", () -> "/root").get(0).typed(), "not taken as an option");
        assertEquals("'it'\\''s'", DoubleClick.quote("it's"));
        assertEquals("notes.txt", DoubleClick.quote("notes.txt"), "a simple name, as typed by hand");
        assertEquals("/var/log/nginx", DoubleClick.quote("/var/log/nginx"));
        assertEquals("città_2-b", DoubleClick.quote("città_2-b"));
        assertEquals("'my files'", DoubleClick.quote("my files"));
        assertEquals("'~/backup'", DoubleClick.quote("~/backup"), "no tilde expansion");
        assertEquals("'a*'", DoubleClick.quote("a*"));
        assertEquals("'$HOME'", DoubleClick.quote("$HOME"));
        assertFalse(DoubleClick.typable("a\\'b"), "fish reads \\' inside quotes");
        assertFalse(DoubleClick.typable("a\nb"));
        assertTrue(DoubleClick.typable("my file (1).txt"));
    }
}
