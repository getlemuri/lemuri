package lemuri.be.dto;

import lemuri.be.sharing.Envelope;

/**
 * Another app seen on the local network. {@code email} is the one it claims: it counts only if the key is trusted.
 * {@code warning}: why it cannot be reached (usually a firewall), null if it answers.
 */
public record Peer(String key, String name, String email, String address, int port, long seen, boolean manual, String warning) {
    /** With no announcements for longer than this, it is no longer considered nearby. */
    public static final long SEEN_MS = 20_000;

    public Peer(String key, String name, String email, String address, int port, long seen, boolean manual) {
        this(key, name, email, address, port, seen, manual, null);
    }

    public Peer withWarning(String a) {
        return new Peer(key, name, email, address, port, seen, manual, a);
    }

    public String fingerprint() {
        return Envelope.fingerprint(key);
    }

    public boolean nearby() {
        return System.currentTimeMillis() - seen < SEEN_MS;
    }

    public String who() {
        return email != null && !email.isBlank() ? email : name;
    }
}
