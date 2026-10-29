package lemuri.be.sharing;

import com.google.gson.JsonObject;
import com.google.gson.JsonParseException;
import com.google.gson.JsonParser;
import java.io.BufferedInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.Inet6Address;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.CompletionException;
import lemuri.be.util.I18n;

/** The local network wire format: one JSON object per line, never longer than {@link #MAX_LINE} bytes. */
final class LanWire {
    /** A message fits in a few KB: whoever sends more is not one of ours. */
    static final int MAX_LINE = 64 * 1024;

    private LanWire() {}

    static void write(OutputStream out, JsonObject m) throws IOException {
        out.write((m + "\n").getBytes(StandardCharsets.UTF_8));
        out.flush();
    }

    static JsonObject read(InputStream in) throws IOException {
        String r = line(in);
        if (r == null) throw new IOException(I18n.t("lan.closed"));
        return parse(r);
    }

    /** A line that is not a JSON object is a broken message, like a dropped connection. */
    private static JsonObject parse(String line) throws IOException {
        try {
            return JsonParser.parseString(line).getAsJsonObject();
        } catch (JsonParseException | IllegalStateException e) {
            throw new IOException("malformed message", e);
        }
    }

    /** One request and its reply, on a connection of its own. */
    static JsonObject ask(String host, int port, JsonObject m, int waitMs) throws IOException {
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(host, port), 3000);
            s.setSoTimeout(waitMs);
            write(s.getOutputStream(), m);
            String line = line(new BufferedInputStream(s.getInputStream()));
            if (line == null) throw new IOException("no answer");
            return parse(line);
        }
    }

    /** A line of text, at most {@link #MAX_LINE} bytes. */
    static String line(InputStream in) throws IOException {
        ByteArrayOutputStream b = new ByteArrayOutputStream();
        for (int c; (c = in.read()) != -1; ) {
            if (c == '\n') return b.toString(StandardCharsets.UTF_8);
            if (b.size() >= MAX_LINE) throw new IOException("message too long");
            b.write(c);
        }
        return b.size() > 0 ? b.toString(StandardCharsets.UTF_8) : null;
    }

    static String text(JsonObject o, String field) {
        // Only a plain value: an object or an array in its place (a message shaped by someone else) counts as missing.
        return o.has(field) && o.get(field).isJsonPrimitive() ? o.get(field).getAsString() : null;
    }

    /** True only if the field is the boolean true: anything else (missing, a string, an object) is a no. */
    static boolean yes(JsonObject o, String field) {
        return o.has(field) && o.get(field).isJsonPrimitive() && o.get(field).getAsJsonPrimitive().isBoolean() && o.get(field).getAsBoolean();
    }

    /** A whole number field, or {@code otherwise} if it is missing, not a number or out of the int range. */
    static int number(JsonObject o, String field, int otherwise) {
        try {
            return o.has(field) && o.get(field).isJsonPrimitive() && o.get(field).getAsJsonPrimitive().isNumber() ? o.get(field).getAsInt() : otherwise;
        } catch (NumberFormatException e) {
            return otherwise;
        }
    }

    static String shorten(String s) {
        if (s == null) return null;
        // Not only the ASCII controls: also zero-width and text direction characters, which would hide what is shown.
        s = s.replaceAll("[\\p{Cc}\\p{Cf}\\p{Zl}\\p{Zp}]", "");
        return s.length() > 80 ? s.substring(0, 80) : s;
    }

    /**
     * A local network address: private IPv4 (10/8, 172.16/12, 192.168/16), link-local,
     * loopback, IPv6 ULA (fc00::/7) or 100.64/10 (CGNAT, also used by Tailscale).
     */
    static boolean isPrivate(InetAddress a) {
        if (a.isLoopbackAddress() || a.isSiteLocalAddress() || a.isLinkLocalAddress()) return true;
        byte[] b = a.getAddress();
        if (a instanceof Inet6Address) return (b[0] & 0xfe) == 0xfc;
        return (b[0] & 0xff) == 100 && (b[1] & 0xc0) == 64;
    }

    static String explain(Exception e) {
        Throwable c = e instanceof CompletionException && e.getCause() != null ? e.getCause() : e;
        return c.getMessage() != null ? c.getMessage() : c.toString();
    }
}
