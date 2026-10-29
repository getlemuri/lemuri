package lemuri.be.sharing;

import com.google.gson.Gson;
import com.google.gson.JsonParseException;
import com.google.gson.reflect.TypeToken;
import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.logging.Level;
import java.util.logging.Logger;
import lemuri.be.dto.Parcel;
import lemuri.be.service.Vault;
import lemuri.be.util.Log;

/**
 * Protection against replays: an opened and signed envelope can still be a copy of an old one, pasted
 * again or sent again over the local network by whoever recorded it. The id and date inside the encryption
 * are checked, and the ids of the envelopes already accepted or rejected are remembered in the vault.
 */
final class DecidedEnvelopes {
    /** An envelope older than this is not accepted: nobody leaves a host waiting for months. */
    static final long MAX_AGE_MS = 30L * 24 * 60 * 60 * 1000;
    private static final long DAY_MS = 24L * 60 * 60 * 1000;
    /** inner id → when it was sent. */
    private static final String ENTRY = "sharing:decided-envelopes";
    private static final Gson GSON = new Gson();
    private static final Logger LOG = Log.of(DecidedEnvelopes.class);

    private final Vault vault;

    DecidedEnvelopes(Vault vault) {
        this.vault = vault;
    }

    /** The i18n key of the reason to reject the parcel, or null if it can be offered. */
    String check(Parcel p) {
        return check(p, load(), System.currentTimeMillis());
    }

    static String check(Parcel p, Map<String, Long> decided, long now) {
        if (p.id == null || p.when <= 0) return "sharing.envelope.old";
        if (now - p.when > MAX_AGE_MS || p.when - now > DAY_MS) return "sharing.envelope.tooOld";
        if (decided.containsKey(p.id)) return "sharing.envelope.replay";
        return null;
    }

    /**
     * Accepted or rejected: the id is remembered until the envelope would be too old anyway.
     * False if it could not be saved: then the same envelope could be offered again.
     */
    boolean remember(Parcel p) {
        if (p == null || p.id == null) return true;
        if (!vault.isOpen()) return false;
        try {
            Map<String, Long> m = load();
            long now = System.currentTimeMillis();
            m.values().removeIf(t -> now - t > MAX_AGE_MS + DAY_MS);
            m.put(p.id, p.when);
            vault.remember(ENTRY, GSON.toJson(m));
            return true;
        } catch (IOException | JsonParseException | IllegalStateException e) {
            // not writable, an unreadable list, or the vault locked in the meantime
            LOG.log(Level.WARNING, "Cannot remember a handled envelope", e);
            return false;
        }
    }

    private Map<String, Long> load() {
        String j = vault.password(ENTRY);
        Map<String, Long> m = j != null ? GSON.fromJson(j, new TypeToken<Map<String, Long>>() {}.getType()) : null;
        return m != null ? new LinkedHashMap<>(m) : new LinkedHashMap<>();
    }
}
