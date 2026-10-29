package lemuri.be.sharing;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.math.BigInteger;
import java.security.KeyFactory;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.spec.NamedParameterSpec;
import java.security.spec.XECPrivateKeySpec;
import java.security.spec.XECPublicKeySpec;
import java.util.Arrays;
import java.util.HexFormat;
import javax.crypto.Cipher;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;
import org.junit.jupiter.api.Test;

/**
 * Known answers: the published test vectors of the primitives, and envelopes sealed by an earlier
 * version that must keep opening. "It opens what it closed" alone would not catch a wrong formula.
 */
class EnvelopeVectorsTest {
    private static final HexFormat HEX = HexFormat.of();

    private static byte[] range(int from, int to) {
        byte[] b = new byte[to - from];
        for (int i = 0; i < b.length; i++) b[i] = (byte) (from + i);
        return b;
    }

    /** RFC 5869, test cases 1 and 2: the first 32 bytes of the output, the only block used. */
    @Test
    void hkdfMatchesRfc5869() throws Exception {
        byte[] ikm = new byte[22];
        Arrays.fill(ikm, (byte) 0x0b);
        assertEquals("3cb25f25faacd57a90434f64d0362f2a2d2d0a90cf1a5a4c5db02d56ecc4c5bf",
                HEX.formatHex(Envelope.hkdf(ikm, range(0x00, 0x0d), range(0xf0, 0xfa))));
        assertEquals("b11e398dc80327a1c8e7f78c596a49344f012eda2d4efad8a050cc4c19afa97c",
                HEX.formatHex(Envelope.hkdf(range(0x00, 0x50), range(0x60, 0xb0), range(0xb0, 0x100))));
    }

    private static PrivateKey privateKey(String hex) throws Exception {
        return KeyFactory.getInstance("X25519").generatePrivate(new XECPrivateKeySpec(NamedParameterSpec.X25519, HEX.parseHex(hex)));
    }

    /** RFC 7748 encodes u little-endian. */
    private static PublicKey publicKey(String hex) throws Exception {
        byte[] b = HEX.parseHex(hex);
        for (int i = 0; i < b.length / 2; i++) {
            byte t = b[i];
            b[i] = b[b.length - 1 - i];
            b[b.length - 1 - i] = t;
        }
        return KeyFactory.getInstance("X25519").generatePublic(new XECPublicKeySpec(NamedParameterSpec.X25519, new BigInteger(1, b)));
    }

    /** RFC 7748, section 6.1: Alice and Bob reach the same secret. */
    @Test
    void x25519MatchesRfc7748() throws Exception {
        String shared = "4a5d9d5ba4ce2de1728e3bf480350f25e07e21c947d19e3376f09b3c1e161742";
        assertEquals(shared, HEX.formatHex(Envelope.agreement(privateKey("77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a"),
                publicKey("de9edb7d7b7dc1b4d35b61c2ece435373f8343c85b78674dadfc7e146f882b4f"))));
        assertEquals(shared, HEX.formatHex(Envelope.agreement(privateKey("5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb"),
                publicKey("8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"))));
    }

    /** The GCM test vectors (McGrew and Viega), test case 14: AES-256, zero key and IV, the cipher as Envelope sets it up. */
    @Test
    void aesGcmMatchesThePublishedVector() throws Exception {
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(new byte[32], "AES"), new GCMParameterSpec(128, new byte[12]));
        assertEquals("cea7403d4d606b6e074ec5d3baf39d18" + "d0d1c8a799996bf0265b98b5d48ab919", HEX.formatHex(c.doFinal(new byte[16])));
    }

    // Throwaway keys and envelopes made with the "lemuri-*" labels: if a change breaks them, old deliveries stop opening.
    private static final Envelope.Keys ALICE = new Envelope.Keys("MCowBQYDK2VuAyEAtxhjHlzQeM3GQbMMIQ4PBXUS2dcQgeiMlj3t37MAG3g=",
            "MC4CAQAwBQYDK2VuBCIEIICTUKmv97bil+cq2xkX22zCiYkUtZhOhFwe4Z7DQjSF");
    private static final Envelope.Keys BOB = new Envelope.Keys("MCowBQYDK2VuAyEA4ac3Lgs+xVG2P+0+m8gbkokxHnYyfWwP1E29OXCysz0=",
            "MC4CAQAwBQYDK2VuBCIEIKTACdIj3nc8JX5kMfs0SMxiOeC38Z8fROiNkKO1dTh6");

    @Test
    void envelopesOfTheFirstVersionStillOpen() throws Exception {
        Envelope.Sealed fromAlice = new Envelope.Sealed("MCowBQYDK2VuAyEAN+C9ihA3cbTaq6I1aeB3U0ramYSsBu0+2mbdJtKYOX0=", "Ye9/mwpqhKGnM9DN",
                "TRlOBpJ5WD/b5UNP7wLUcijmpyaN0sdnwUc=");
        assertEquals("from alice", Envelope.openFrom(fromAlice, BOB, ALICE.publicKey(), ALICE.publicKey(), BOB.publicKey()));
    }

    /** The pairing code and the fingerprint are compared between two PCs: both sides must compute the same as before. */
    @Test
    void pairingValuesOfTheFirstVersion() {
        assertEquals("ead57d8f9a786aa73700bd7bf847c8f504c53dfda231797a358cfadf5099359f", Envelope.commitment(BOB.publicKey(), ALICE.publicKey(), "bm9uY2UtYg=="));
        assertEquals("273 556", Envelope.code(ALICE.publicKey(), BOB.publicKey(), "bm9uY2UtYQ==", "bm9uY2UtYg=="));
        assertEquals("4BAC 4C42 6D6B B4C4 F044 0E1C E119 3DA5", Envelope.fingerprint(ALICE.publicKey()));
    }
}
