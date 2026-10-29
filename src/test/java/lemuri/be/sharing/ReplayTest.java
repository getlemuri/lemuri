package lemuri.be.sharing;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import java.util.Map;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;
import lemuri.be.dto.Parcel;

/** A real envelope pasted or sent again by whoever kept a copy must not look like a new host. */
class ReplayTest {
    private static Parcel sent() {
        Host h = new Host();
        h.name = "web-01";
        h.address = "203.0.113.21";
        h.user = "deploy";
        return Parcel.from(h, null, null);
    }

    @Test
    void everyDeliveryHasIdAndDateInside() {
        Parcel a = sent(), b = sent();
        assertNotEquals(a.id, b.id);
        assertNull(DecidedEnvelopes.check(a, Map.of(), a.when + 60_000));
    }

    @Test
    void aCopyOfAnAlreadyDecidedEnvelopeIsRejected() {
        Parcel p = sent();
        assertEquals("sharing.envelope.replay", DecidedEnvelopes.check(p, Map.of(p.id, p.when), p.when + 60_000));
    }

    @Test
    void anOldOrIdlessEnvelopeIsRejected() {
        Parcel p = sent();
        assertEquals("sharing.envelope.tooOld", DecidedEnvelopes.check(p, Map.of(), p.when + DecidedEnvelopes.MAX_AGE_MS + 1));
        p.id = null;
        assertEquals("sharing.envelope.old", DecidedEnvelopes.check(p, Map.of(), System.currentTimeMillis()));
    }
}
