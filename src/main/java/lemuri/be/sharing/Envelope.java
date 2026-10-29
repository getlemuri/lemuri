package lemuri.be.sharing;

import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyFactory;
import java.security.KeyPair;
import java.security.KeyPairGenerator;
import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.security.PrivateKey;
import java.security.PublicKey;
import java.security.SecureRandom;
import java.security.spec.PKCS8EncodedKeySpec;
import java.security.spec.X509EncodedKeySpec;
import java.util.Arrays;
import java.util.Base64;
import java.util.Locale;
import javax.crypto.Cipher;
import javax.crypto.KeyAgreement;
import javax.crypto.Mac;
import javax.crypto.spec.GCMParameterSpec;
import javax.crypto.spec.SecretKeySpec;

/**
 * Encryption for a single recipient, like a sealed envelope: X25519 with a
 * throwaway key and with the sender's key, HKDF-SHA256 and AES-256-GCM. The
 * sender keeps nothing; only the recipient's private key opens the envelope,
 * and only if it was sealed by the sender's. Sender and recipient are part of
 * the authenticated data: an envelope moved to another pair does not open.
 * Specified in docs/crypto.md.
 */
public final class Envelope {
    private static final byte[] LAN_INFO = "lemuri-lan-v1".getBytes(StandardCharsets.US_ASCII);
    private static final SecureRandom RANDOM = new SecureRandom();
    private static final Base64.Encoder B64 = Base64.getEncoder();
    private static final Base64.Decoder D64 = Base64.getDecoder();

    /** What travels: throwaway public key, IV and encrypted data, in base64. */
    public record Sealed(String ephemeral, String iv, String data) {}

    /** The user's key pair in base64: the public key goes to the contacts, the private one into the vault. */
    public record Keys(String publicKey, String privateKey) {}

    private Envelope() {}

    public static Keys newKeys() throws GeneralSecurityException {
        KeyPair k = KeyPairGenerator.getInstance("X25519").generateKeyPair();
        return new Keys(B64.encodeToString(k.getPublic().getEncoded()), B64.encodeToString(k.getPrivate().getEncoded()));
    }

    /**
     * Like {@link #close}, but the envelope also proves who sends it: the sender's key is
     * added to the throwaway one. Every delivery uses it: there is no server to say who wrote it.
     */
    public static Sealed sealFrom(String text, Keys mine, String publicRecipient, String from, String a) throws GeneralSecurityException {
        PublicKey dest = publish(publicRecipient);
        KeyPair eph = KeyPairGenerator.getInstance("X25519").generateKeyPair();
        byte[] key = deriveFrom(eph.getPrivate(), privateKey(mine), dest, dest, eph.getPublic().getEncoded(), dest.getEncoded(), D64.decode(mine.publicKey()));
        byte[] iv = new byte[12];
        RANDOM.nextBytes(iv);
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(Cipher.ENCRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, iv));
        c.updateAAD(aad(from, a));
        byte[] data = c.doFinal(text.getBytes(StandardCharsets.UTF_8));
        Arrays.fill(key, (byte) 0);
        return new Sealed(B64.encodeToString(eph.getPublic().getEncoded()), B64.encodeToString(iv), B64.encodeToString(data));
    }

    /** Opens only if it was really sealed by whoever holds the private key of {@code publicSender}. */
    public static String openFrom(Sealed b, Keys mine, String publicSender, String from, String a) throws GeneralSecurityException {
        PublicKey eph = publish(b.ephemeral()), sender = publish(publicSender);
        PrivateKey priv = privateKey(mine);
        byte[] key = deriveFrom(priv, priv, eph, sender, eph.getEncoded(), D64.decode(mine.publicKey()), sender.getEncoded());
        Cipher c = Cipher.getInstance("AES/GCM/NoPadding");
        c.init(Cipher.DECRYPT_MODE, new SecretKeySpec(key, "AES"), new GCMParameterSpec(128, D64.decode(b.iv())));
        c.updateAAD(aad(from, a));
        try {
            return new String(c.doFinal(D64.decode(b.data())), StandardCharsets.UTF_8);
        } finally {
            Arrays.fill(key, (byte) 0);
        }
    }

    /** A random number for a pairing: 32 bytes in base64. */
    public static String nonce() {
        byte[] n = new byte[32];
        RANDOM.nextBytes(n);
        return B64.encodeToString(n);
    }

    /**
     * The commitment of whoever receives a pairing request: it binds their random number to the two keys
     * before seeing the other one. So a man in the middle cannot pick the numbers to make the same code
     * appear on both screens (like Bluetooth Numeric Comparison).
     */
    public static String commitment(String publicMine, String publicOther, String myNonce) {
        return hex(sha256("lemuri-commitment-v2\n" + publicMine + "\n" + publicOther + "\n" + myNonce));
    }

    /**
     * 6-digit code to pair two PCs, from {@code publicAsker} (who asked) to {@code publicResponder}, with
     * the two random numbers. The same on both screens if nobody is in the middle, except with a one in a million chance.
     */
    public static String code(String publicAsker, String publicResponder, String nonceAsker, String nonceResponder) {
        byte[] h = sha256("lemuri-pairing-v2\n" + publicAsker + "\n" + publicResponder + "\n" + nonceAsker + "\n" + nonceResponder);
        int n = ((h[0] & 0xff) << 24 | (h[1] & 0xff) << 16 | (h[2] & 0xff) << 8 | (h[3] & 0xff)) & 0x7fffffff;
        String c = String.format("%06d", n % 1_000_000);
        return c.substring(0, 3) + " " + c.substring(3);
    }

    private static byte[] sha256(String text) {
        try {
            return MessageDigest.getInstance("SHA-256").digest(text.getBytes(StandardCharsets.US_ASCII));
        } catch (NoSuchAlgorithmException e) {
            throw new IllegalStateException(e);
        }
    }

    private static String hex(byte[] b) {
        StringBuilder s = new StringBuilder();
        for (byte x : b) s.append(String.format("%02x", x));
        return s.toString();
    }

    /**
     * Fingerprint of a public key, to compare by voice with the contact: if it matches, the key that arrived
     * is really theirs. 128 bits (the first 16 bytes of SHA-256, in groups of four hex digits): with 64,
     * making a key with the same fingerprint as someone else's is within reach of a determined attacker.
     */
    public static String fingerprint(String publish) {
        try {
            byte[] h = MessageDigest.getInstance("SHA-256").digest(D64.decode(publish));
            StringBuilder s = new StringBuilder();
            for (int i = 0; i < 16; i++) {
                if (i > 0 && i % 2 == 0) s.append(' ');
                s.append(String.format("%02X", h[i]));
            }
            return s.toString();
        } catch (IllegalArgumentException | GeneralSecurityException e) {
            return "?";
        }
    }

    private static PublicKey publish(String b64) throws GeneralSecurityException {
        return KeyFactory.getInstance("X25519").generatePublic(new X509EncodedKeySpec(D64.decode(b64)));
    }

    private static PrivateKey privateKey(Keys mine) throws GeneralSecurityException {
        return KeyFactory.getInstance("X25519").generatePrivate(new PKCS8EncodedKeySpec(D64.decode(mine.privateKey())));
    }

    /**
     * Two X25519 agreements: throwaway key with recipient, and sender with
     * recipient. On the sending side: (ephemeral, mine, recipient, recipient); on the
     * receiving side: (mine, mine, ephemeral, sender). The salt binds the key to the three public keys.
     */
    private static byte[] deriveFrom(PrivateKey privOne, PrivateKey privTwo, PublicKey pubOne, PublicKey pubTwo,
            byte[] pubEphemeral, byte[] pubRecipient, byte[] pubSender) throws GeneralSecurityException {
        byte[] one = agreement(privOne, pubOne), two = agreement(privTwo, pubTwo);
        byte[] secret = new byte[one.length + two.length];
        System.arraycopy(one, 0, secret, 0, one.length);
        System.arraycopy(two, 0, secret, one.length, two.length);
        byte[] salt = new byte[pubEphemeral.length + pubRecipient.length + pubSender.length];
        System.arraycopy(pubEphemeral, 0, salt, 0, pubEphemeral.length);
        System.arraycopy(pubRecipient, 0, salt, pubEphemeral.length, pubRecipient.length);
        System.arraycopy(pubSender, 0, salt, pubEphemeral.length + pubRecipient.length, pubSender.length);
        byte[] okm = hkdf(secret, salt, LAN_INFO);
        Arrays.fill(one, (byte) 0);
        Arrays.fill(two, (byte) 0);
        Arrays.fill(secret, (byte) 0);
        return okm;
    }

    /** X25519 (RFC 7748). Package-private for the test vectors. */
    static byte[] agreement(PrivateKey priv, PublicKey pub) throws GeneralSecurityException {
        KeyAgreement ka = KeyAgreement.getInstance("X25519");
        ka.init(priv);
        ka.doPhase(pub, true);
        return ka.generateSecret();
    }

    /** HKDF-SHA256 (RFC 5869) with one output block: 32 bytes, the AES-256 key. Package-private for the test vectors. */
    static byte[] hkdf(byte[] secret, byte[] salt, byte[] info) throws GeneralSecurityException {
        Mac mac = Mac.getInstance("HmacSHA256");
        mac.init(new SecretKeySpec(salt, "HmacSHA256"));
        byte[] prk = mac.doFinal(secret);
        mac.init(new SecretKeySpec(prk, "HmacSHA256"));
        mac.update(info);
        mac.update((byte) 1);
        byte[] okm = mac.doFinal();
        Arrays.fill(prk, (byte) 0);
        return okm;
    }

    private static byte[] aad(String from, String a) {
        return (from.toLowerCase(Locale.ROOT) + "\n" + a.toLowerCase(Locale.ROOT)).getBytes(StandardCharsets.UTF_8);
    }
}
