package lemuri.be.dto;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertTrue;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import java.util.Set;
import org.junit.jupiter.api.Test;

/**
 * The saved and exchanged formats use English field names. hosts.json and the vault written by the first
 * versions used Italian ones: they must still be read, and are rewritten in English at the next save.
 */
class SavedFormatTest {
    private static final Gson GSON = new Gson();

    @Test
    void hostIsSavedWithEnglishFieldNames() {
        Host h = new Host();
        h.name = "web-01";
        h.address = "203.0.113.21";
        JsonObject o = GSON.toJsonTree(h).getAsJsonObject();
        assertEquals(Set.of("name", "group", "address", "port", "user", "authMethod", "note", "state", "received"), o.keySet());
        assertEquals("KEY", o.get("authMethod").getAsString());
        assertEquals("UNKNOWN", o.get("state").getAsString());
    }

    @Test
    void aHostFileOfTheFirstVersionsIsStillRead() {
        Host h = GSON.fromJson("{\"nome\":\"db\",\"indirizzo\":\"203.0.113.5\",\"porta\":2222,\"utente\":\"deploy\","
                + "\"autenticazione\":\"CHIAVE\",\"chiave\":\"id_ed25519\",\"gruppo\":\"Prod\",\"stato\":\"SCONOSCIUTO\",\"daCollega\":\"a@b.c\"}", Host.class);
        assertEquals("db", h.name);
        assertEquals(2222, h.port);
        assertEquals(Host.AuthMethod.KEY, h.authMethod);
        assertEquals("id_ed25519", h.key);
        assertEquals("Prod", h.group);
        assertEquals(HostStatus.UNKNOWN, h.state);
        assertEquals("a@b.c", h.fromContact);
    }

    /** Hosts saved before the rename keep who sent them: "fromColleague" is read as fromContact. */
    @Test
    void fromColleagueIsReadAsFromContact() {
        Host h = GSON.fromJson("{\"name\":\"db\",\"fromColleague\":\"bruno\"}", Host.class);
        assertEquals("bruno", h.fromContact);
        assertTrue(GSON.toJson(h).contains("\"fromContact\":\"bruno\""));
    }

    @Test
    void parcelAndTrustedPeerUseEnglishFieldNames() {
        Host h = new Host();
        h.name = "web-01";
        h.address = "203.0.113.21";
        JsonObject p = GSON.toJsonTree(Parcel.from(h, "secret", "hello")).getAsJsonObject();
        assertEquals(Set.of("name", "address", "user", "password", "message", "port", "id", "when", "authMethod"), p.keySet());
        assertEquals(Set.of("key", "name"), GSON.toJsonTree(new TrustedPeer("k", "n")).getAsJsonObject().keySet());
        assertEquals("n", GSON.fromJson("{\"chiave\":\"k\",\"nome\":\"n\"}", TrustedPeer.class).name());
    }
}
