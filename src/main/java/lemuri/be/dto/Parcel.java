package lemuri.be.dto;

import java.util.UUID;


/** What an envelope contains: the host without id or status, and maybe the password. */
public final class Parcel {
    public String name;
    public String address;
    public String user;
    public String os;
    public String password;
    public String message;
    public int port = 22;
    /**
     * A random id and the time of sending, INSIDE the encrypted and signed content: whoever kept a copy of a
     * real envelope can paste it or send it again months later, but cannot change these.
     */
    public String id;
    public long when;
    public Host.AuthMethod authMethod;

    public static Parcel from(Host h, String password, String message) {
        Parcel p = new Parcel();
        p.name = h.name;
        p.address = h.address;
        p.port = h.port;
        p.user = h.user;
        p.authMethod = h.authMethod;
        p.os = h.os;
        p.password = password;
        p.message = message;
        p.id = UUID.randomUUID().toString();
        p.when = System.currentTimeMillis();
        return p;
    }

    /**
     * For a parcel that has just arrived: cleans the free texts ({@link #clean}), then says what is wrong with
     * it ({@link #problem}), or null. The one way in for a host from a contact.
     */
    public String admit() {
        clean();
        return problem();
    }

    /**
     * Name, system and message are shown when the user decides whether to trust the host: zero-width and text
     * direction characters (U+202E can make "root@evil" read like another address) go, line breaks and other
     * controls become spaces. The message keeps its line breaks.
     */
    void clean() {
        name = text(name, false);
        os = text(os, false);
        message = text(message, true);
    }

    private static String text(String s, boolean lines) {
        if (s == null) return null;
        StringBuilder b = new StringBuilder();
        s.codePoints().forEach(c -> {
            int type = Character.getType(c);
            if (type == Character.FORMAT || type == Character.PRIVATE_USE || type == Character.UNASSIGNED || type == Character.SURROGATE) return;
            if (lines && c == '\n') b.append('\n');
            else if (type == Character.CONTROL || type == Character.LINE_SEPARATOR || type == Character.PARAGRAPH_SEPARATOR) b.append(' ');
            else b.appendCodePoint(c);
        });
        return b.toString().strip();
    }

    /**
     * What is wrong with an incoming parcel, or null if it is fine. Parcels come only from contacts, but an old version or a bug on the other side must not bring into the list a host without an
     * address, with port 0 or with a one-megabyte message. Same bounds as the host dialog ({@code Ui.MAX_*}).
     */
    public String problem() {
        if (empty(name) || empty(address) || empty(user)) return "name, address and user are required";
        if (port < 1 || port > 65535) return "port out of range: " + port;
        if (name.length() > 64 || address.length() > 255 || user.length() > 64) return "name, address or user too long";
        // \p{Cntrl} alone is only Cc: also format (bidi, zero-width) and separator characters, which hide what is shown.
        if (!address.matches("[^\\s\\p{Cc}\\p{Cf}\\p{Z}]+") || !user.matches("[^\\s\\p{Cc}\\p{Cf}\\p{Z}]+")) return "address or user with spaces or control characters";
        if (tooLong(os, 200) || tooLong(password, 4096) || tooLong(message, 2000)) return "a field is too long";
        if (tooLong(id, 64)) return "id too long";
        return null;
    }

    private static boolean empty(String s) {
        return s == null || s.isBlank();
    }

    private static boolean tooLong(String s, int max) {
        return s != null && s.length() > max;
    }

    /**
     * The host for this PC. Never with the sender's key path: it is a file on their PC, and here it could name
     * any file of the recipient. With a key the recipient's own are used (agent or ~/.ssh), or the one they choose
     * by editing the host.
     */
    public Host asHost() {
        Host h = new Host();
        h.name = name;
        h.address = address;
        h.port = port;
        h.user = user;
        h.authMethod = authMethod != null ? authMethod : Host.AuthMethod.PASSWORD;
        h.os = os;
        return h;
    }
}
