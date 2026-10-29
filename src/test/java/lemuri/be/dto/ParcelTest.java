package lemuri.be.dto;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;

import com.google.gson.Gson;
import org.junit.jupiter.api.Test;
import lemuri.fe.v.Ui;

/** A host arrived from a contact or from a paired PC is checked before being offered. */
class ParcelTest {
    private static Parcel good() {
        Parcel p = new Parcel();
        p.name = "web-01";
        p.address = "203.0.113.21";
        p.user = "deploy";
        return p;
    }

    @Test
    void aNormalHostIsFine() {
        assertNull(good().problem());
    }

    @Test
    void missingFieldsOrPortOutOfRangeRejected() {
        for (int port : new int[] {0, -1, 65536, 99999}) {
            Parcel p = good();
            p.port = port;
            assertNotNull(p.problem(), "port " + port);
        }
        Parcel withoutAddress = good();
        withoutAddress.address = null;
        assertNotNull(withoutAddress.problem());
        Parcel emptyUser = good();
        emptyUser.user = "  ";
        assertNotNull(emptyUser.problem());
    }

    @Test
    void tooLongOrOddTextsRejected() {
        Parcel message = good();
        message.message = "x".repeat(1_000_000);
        assertNotNull(message.problem());
        Parcel address = good();
        address.address = "host\n-oProxyCommand=x";
        assertNotNull(address.problem());
    }

    /** Analysis #13, 2.1: what the acceptance dialog shows cannot be disguised with invisible or bidi characters. */
    @Test
    void invisibleAndDirectionCharactersDoNotDisguiseTheHost() {
        Parcel bidiAddress = good();
        bidiAddress.address = "1.2.3.4\u202E";
        assertNotNull(bidiAddress.admit());
        Parcel zeroWidthUser = good();
        zeroWidthUser.user = "ro\u200Bot";
        assertNotNull(zeroWidthUser.admit());
        Parcel lineSeparator = good();
        lineSeparator.address = "1.2.3.4\u2028evil";
        assertNotNull(lineSeparator.admit());

        Parcel texts = good();
        texts.name = "prod-db\u202E\u200B";
        texts.os = "deb\u200Bian";
        texts.message = "first line\nok\u202Eevil\u0007";
        assertNull(texts.admit());
        assertEquals("prod-db", texts.name);
        assertEquals("debian", texts.os);
        assertEquals("first line\nokevil", texts.message);
        Parcel twoLines = good();
        twoLines.name = "line1\nline2";
        assertNull(twoLines.admit());
        assertEquals("line1 line2", twoLines.name);
        // A name made only of invisible characters is a missing name.
        Parcel invisible = good();
        invisible.name = "\u200B\u202E";
        assertNotNull(invisible.admit());
    }

    /** Analysis #14, 2.1: the sender's key path is a file on their PC; here it could name any file of the recipient. */
    @Test
    void theKeyPathOfTheSenderNeverArrives() {
        Host h = new Host();
        h.name = "web-01";
        h.address = "203.0.113.21";
        h.user = "deploy";
        h.authMethod = Host.AuthMethod.KEY;
        h.key = "/home/sender/.ssh/id_ed25519";
        Gson gson = new Gson();
        String sent = gson.toJson(Parcel.from(h, null, null));
        assertFalse(sent.contains("id_ed25519"));
        // An older version still sends it: it is ignored.
        Parcel old = gson.fromJson("{\"name\":\"web-01\",\"address\":\"203.0.113.21\",\"user\":\"deploy\","
                + "\"authMethod\":\"KEY\",\"key\":\"/home/recipient/.ssh/id_ed25519\\u202Eevil\"}", Parcel.class);
        assertNull(old.admit());
        Host arrived = old.asHost();
        assertEquals(Host.AuthMethod.KEY, arrived.authMethod);
        assertNull(arrived.key);
    }

    /** Analysis #14, 2.2: the same bounds as the host dialog, so a host that arrived can be edited and saved as it is. */
    @Test
    void sameBoundsAsTheHostDialog() {
        Parcel name = good();
        name.name = "n".repeat(Ui.MAX_NAME);
        assertNull(name.problem());
        name.name += "n";
        assertNotNull(name.problem());
        Parcel user = good();
        user.user = "u".repeat(Ui.MAX_USER + 1);
        assertNotNull(user.problem());
        Parcel address = good();
        address.address = "a".repeat(Ui.MAX_ADDRESS + 1);
        assertNotNull(address.problem());
    }
}
