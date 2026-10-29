package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.io.File;
import java.util.List;
import org.freedesktop.dbus.types.Variant;
import org.junit.jupiter.api.Test;

/** The file portal's reply: local files only, whatever the shape of the list. */
class PortalFilesTest {
    @Test
    void localUrisBecomeFilesAndTheRestIsLeftOut() {
        Variant<?> uris = new Variant<>(new String[] {"file:///home/me/a%20b.txt", "sftp://host/x", "file:///tmp/c"}, "as");
        assertEquals(List.of(new File("/home/me/a b.txt"), new File("/tmp/c")), PortalFiles.files(uris));
        assertEquals(List.of(), PortalFiles.files(null));
    }
}
