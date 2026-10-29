package lemuri.be.sharing;

import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.nio.charset.StandardCharsets;
import java.security.GeneralSecurityException;
import java.security.KeyFactory;
import java.security.spec.X509EncodedKeySpec;
import java.util.Base64;
import lemuri.be.util.ContactNames;

/**
 * The text forms that people pass on by hand, by email or chat: a contact's public key on one line,
 * and a host sealed for one contact between BEGIN and END lines. The channel does not matter: the key
 * is checked by its fingerprint, the host is readable only by the recipient and signed by the sender.
 */
public final class TextEnvelope {
    static final String KEY_PREFIX = "LEMURI-KEY 1 ";
    public static final String BEGIN = "-----BEGIN LEMURI HOST-----", END = "-----END LEMURI HOST-----";
    /** A host envelope is a few KB: beyond this the text is not one. */
    public static final int MAX_TEXT = 64 * 1024;

    private TextEnvelope() {}

    /** A public key with the name its owner suggests. */
    public record KeyCard(String key, String name) {}

    /** What travels in a host envelope: who sealed it, for whom, and the sealed content. */
    record Posted(String from, String to, Envelope.Sealed sealed) {}

    public static String keyCard(String publicKey, String name) {
        return KEY_PREFIX + publicKey + " " + ContactNames.clean(name);
    }

    /** The key card found in {@code text}, or null if there is none or the key is not an X25519 public key. */
    public static KeyCard parseKey(String text) {
        if (text == null) return null;
        for (String line : text.split("\\R")) {
            String l = line.strip();
            if (!l.startsWith(KEY_PREFIX)) continue;
            String[] p = l.substring(KEY_PREFIX.length()).split(" ", 2);
            if (!isKey(p[0])) return null;
            return new KeyCard(p[0], p.length > 1 ? ContactNames.clean(p[1]) : "");
        }
        return null;
    }

    static String armor(String from, String to, Envelope.Sealed b) {
        JsonObject o = new JsonObject();
        o.addProperty("v", 1);
        o.addProperty("from", from);
        o.addProperty("to", to);
        o.addProperty("ephemeral", b.ephemeral());
        o.addProperty("iv", b.iv());
        o.addProperty("data", b.data());
        String body = Base64.getMimeEncoder(64, "\n".getBytes(StandardCharsets.US_ASCII))
                .encodeToString(o.toString().getBytes(StandardCharsets.UTF_8));
        return BEGIN + "\n" + body + "\n" + END + "\n";
    }

    /** The envelope between BEGIN and END in {@code text}, wherever it was pasted; null if there is none or it is malformed. */
    static Posted unarmor(String text) {
        if (text == null || text.length() > MAX_TEXT) return null;
        int a = text.indexOf(BEGIN), z = text.indexOf(END);
        if (a < 0 || z < a) return null;
        try {
            // Mail and chat programs re-wrap lines and quote them with "> ": only base64 characters count.
            String body = text.substring(a + BEGIN.length(), z).replaceAll("[^A-Za-z0-9+/=]", "");
            JsonObject o = JsonParser.parseString(new String(Base64.getDecoder().decode(body), StandardCharsets.UTF_8)).getAsJsonObject();
            if (o.get("v").getAsInt() != 1) return null;
            String from = o.get("from").getAsString(), to = o.get("to").getAsString();
            if (!isKey(from) || !isKey(to)) return null;
            return new Posted(from, to, new Envelope.Sealed(o.get("ephemeral").getAsString(), o.get("iv").getAsString(), o.get("data").getAsString()));
        } catch (RuntimeException e) {
            return null;
        }
    }

    static boolean isKey(String b64) {
        try {
            KeyFactory.getInstance("X25519").generatePublic(new X509EncodedKeySpec(Base64.getDecoder().decode(b64)));
            return true;
        } catch (GeneralSecurityException | IllegalArgumentException e) {
            return false;
        }
    }
}
