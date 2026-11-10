package lemuri.be.update;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.IOException;
import lemuri.be.dto.Release;
import org.junit.jupiter.api.Test;

class VersionsTest {
    @Test
    void order() {
        assertTrue(Versions.compare("0.2.0", "0.1.9") > 0);
        assertTrue(Versions.compare("0.10.0", "0.9.0") > 0);
        assertTrue(Versions.compare("1.0", "0.99.99") > 0);
        assertEquals(0, Versions.compare("0.2", "0.2.0"));
        assertTrue(Versions.compare("0.1.0", "0.1.1") < 0);
    }

    @Test
    void onlyPlainVersions() {
        assertTrue(Versions.valid("0.2.0"));
        assertFalse(Versions.valid("0.2.0-SNAPSHOT"));
        assertFalse(Versions.valid("0.2.0-rc1"));
        assertFalse(Versions.valid(""));
        assertFalse(Versions.valid(null));
    }

    @Test
    void releaseFromGitHub() throws IOException {
        Release r = GitUpdater.release("""
                {"tag_name":"v0.2.0","html_url":"https://github.com/getlemuri/lemuri/releases/tag/v0.2.0","body":"### Added\\n- x"}""");
        assertEquals("v0.2.0", r.tag());
        assertEquals("0.2.0", r.version());
        assertEquals("### Added\n- x", r.notes());
    }

    @Test
    void foreignPagesAreNotOpened() throws IOException {
        Release r = GitUpdater.release("{\"tag_name\":\"v0.2.0\",\"html_url\":\"https://evil.example/x\"}");
        assertEquals("https://github.com/getlemuri/lemuri/releases/tag/v0.2.0", r.url());
        assertEquals("", r.notes());
    }

    @Test
    void oddTagsAreRefused() {
        assertThrows(IOException.class, () -> GitUpdater.release("{\"tag_name\":\"v0.2.0-rc1\"}"));
        assertThrows(IOException.class, () -> GitUpdater.release("{\"tag_name\":\"latest\"}"));
        assertThrows(IOException.class, () -> GitUpdater.release("not json"));
    }
}
