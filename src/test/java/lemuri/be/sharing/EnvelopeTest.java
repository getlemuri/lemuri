package lemuri.be.sharing;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.security.GeneralSecurityException;
import java.util.Base64;
import org.junit.jupiter.api.Test;

class EnvelopeTest {
    private final Envelope.Keys anna = keys(), bruno = keys(), carla = keys();

    private static Envelope.Keys keys() {
        try {
            return Envelope.newKeys();
        } catch (GeneralSecurityException e) {
            throw new AssertionError(e);
        }
    }

    /** The same envelope with one byte of the encrypted data changed. */
    private static Envelope.Sealed tampered(Envelope.Sealed b) {
        byte[] d = Base64.getDecoder().decode(b.data());
        d[0] ^= 1;
        return new Envelope.Sealed(b.ephemeral(), b.iv(), Base64.getEncoder().encodeToString(d));
    }

    private Envelope.Sealed fromAnna(String text) throws GeneralSecurityException {
        return Envelope.sealFrom(text, anna, bruno.publicKey(), anna.publicKey(), bruno.publicKey());
    }

    @Test
    void opensWithTheRightRecipient() throws Exception {
        assertEquals("password segreta", Envelope.openFrom(fromAnna("password segreta"), bruno, anna.publicKey(), anna.publicKey(), bruno.publicKey()));
    }

    @Test
    void doesNotOpenWithAnotherKey() throws Exception {
        Envelope.Sealed b = fromAnna("password segreta");
        assertThrows(GeneralSecurityException.class, () -> Envelope.openFrom(b, carla, anna.publicKey(), anna.publicKey(), bruno.publicKey()));
    }

    @Test
    void senderAndRecipientAreBoundToTheEnvelope() throws Exception {
        Envelope.Sealed b = fromAnna("password segreta");
        assertThrows(GeneralSecurityException.class, () -> Envelope.openFrom(b, bruno, anna.publicKey(), carla.publicKey(), bruno.publicKey()));
        assertThrows(GeneralSecurityException.class, () -> Envelope.openFrom(b, bruno, anna.publicKey(), anna.publicKey(), carla.publicKey()));
    }

    @Test
    void everyEnvelopeDiffersEvenWithTheSameText() throws Exception {
        Envelope.Sealed x = fromAnna("uguale"), y = fromAnna("uguale");
        assertNotEquals(x.data(), y.data());
        assertNotEquals(x.ephemeral(), y.ephemeral());
    }

    @Test
    void withSenderOpensOnlyWithItsKey() throws Exception {
        Envelope.Sealed b = Envelope.sealFrom("host", anna, bruno.publicKey(), anna.publicKey(), bruno.publicKey());
        assertEquals("host", Envelope.openFrom(b, bruno, anna.publicKey(), anna.publicKey(), bruno.publicKey()));
        // Carla cannot pose as Anna.
        Envelope.Sealed fake = Envelope.sealFrom("host", carla, bruno.publicKey(), anna.publicKey(), bruno.publicKey());
        assertThrows(GeneralSecurityException.class, () -> Envelope.openFrom(fake, bruno, anna.publicKey(), anna.publicKey(), bruno.publicKey()));
        assertThrows(GeneralSecurityException.class, () -> Envelope.openFrom(tampered(b), bruno, anna.publicKey(), anna.publicKey(), bruno.publicKey()));
    }

    @Test
    void codeDependsOnKeysAndRandomNumbers() {
        String na = Envelope.nonce(), nb = Envelope.nonce();
        String c = Envelope.code(anna.publicKey(), bruno.publicKey(), na, nb);
        assertTrue(c.matches("\\d{3} \\d{3}"), c);
        assertEquals(c, Envelope.code(anna.publicKey(), bruno.publicKey(), na, nb));
        // With another random number (or another key) the code changes: a man in the middle cannot choose it.
        assertNotEquals(c, Envelope.code(anna.publicKey(), bruno.publicKey(), na, Envelope.nonce()));
        assertNotEquals(c, Envelope.code(anna.publicKey(), carla.publicKey(), na, nb));
        assertNotEquals(Envelope.nonce(), Envelope.nonce());
    }

    @Test
    void commitmentBindsTheNumberToTheKeys() {
        String nb = Envelope.nonce();
        String i = Envelope.commitment(bruno.publicKey(), anna.publicKey(), nb);
        assertEquals(i, Envelope.commitment(bruno.publicKey(), anna.publicKey(), nb));
        assertNotEquals(i, Envelope.commitment(bruno.publicKey(), anna.publicKey(), Envelope.nonce()));
        assertNotEquals(i, Envelope.commitment(bruno.publicKey(), carla.publicKey(), nb));
    }

    @Test
    void fingerprintIsStableAnd128Bits() {
        String i = Envelope.fingerprint(anna.publicKey());
        assertEquals(i, Envelope.fingerprint(anna.publicKey()));
        assertTrue(i.matches("[0-9A-F]{4}( [0-9A-F]{4}){7}"), i);
    }
}
