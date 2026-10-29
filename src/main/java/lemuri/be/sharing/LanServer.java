package lemuri.be.sharing;

import static lemuri.be.sharing.LanWire.isPrivate;
import static lemuri.be.sharing.LanWire.line;
import static lemuri.be.sharing.LanWire.text;
import static lemuri.be.sharing.LanWire.write;

import com.google.gson.Gson;
import com.google.gson.JsonObject;
import com.google.gson.JsonParser;
import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.ServerSocket;
import java.net.Socket;
import java.security.GeneralSecurityException;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.RejectedExecutionException;
import java.util.concurrent.ScheduledExecutorService;
import java.util.concurrent.ScheduledFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicReference;
import lemuri.be.dto.Delivery;
import lemuri.be.dto.Parcel;

/**
 * The incoming side of the local network: accepts connections from private addresses only and answers
 * "hello", pairing requests and deliveries. A delivery is opened only if it comes from a trusted key, and
 * an old or already seen one is rejected as a replay.
 */
final class LanServer {
    private static final Gson GSON = new Gson();

    private final Lan lan;
    /** id of the received envelopes → when: a repeat is rejected. Beyond 10 minutes they are rejected anyway, and forgotten. */
    private final Map<String, Long> alreadyArrived = new ConcurrentHashMap<>();
    /** Last connection received from another PC: proof that the firewall here lets traffic in. */
    private volatile long lastEntry;

    LanServer(Lan lan) {
        this.lan = lan;
    }

    long lastEntry() {
        return lastEntry;
    }

    /** Forgets the envelope ids old enough to be rejected anyway. */
    void prune(long now) {
        alreadyArrived.values().removeIf(t -> now - t > 20 * 60_000);
    }

    /** Accepts connections until the socket is closed; each one is served by {@code jobs}, or refused if it is full. */
    void listen(ServerSocket s, ExecutorService jobs, ScheduledExecutorService times) {
        while (!s.isClosed()) {
            try {
                Socket c = s.accept();
                // Local network only: the port is open on all interfaces, but nobody outside gets an answer.
                if (!isPrivate(c.getInetAddress())) {
                    c.close();
                    continue;
                }
                if (!c.getInetAddress().isLoopbackAddress()) lastEntry = System.currentTimeMillis();
                try {
                    jobs.execute(() -> serve(c, times));
                } catch (RejectedExecutionException full) {
                    c.close();
                }
            } catch (IOException e) {
                // closed
            }
        }
    }

    private void serve(Socket c, ScheduledExecutorService t) {
        // One deadline for the whole connection, not for every read: whoever sends a byte now and then does not hold a thread for hours.
        var deadline = new AtomicReference<ScheduledFuture<?>>(schedule(t, c, 15));
        try (c) {
            c.setSoTimeout(10_000);
            InputStream in = new BufferedInputStream(c.getInputStream());
            OutputStream out = c.getOutputStream();
            String line = line(in);
            if (line == null) return;
            JsonObject m = JsonParser.parseString(line).getAsJsonObject();
            String from = c.getInetAddress().getHostAddress();
            JsonObject r;
            switch (String.valueOf(text(m, "type"))) {
                case "hello" -> {
                    lan.discovery().notice(m, from, false);
                    r = lan.identity("me");
                }
                // Pairing waits for the user: more time, but only after the random numbers are exchanged. Before that
                // the 15 seconds apply, so whoever connects and stays silent does not hold the pairing without showing up.
                case "pair" -> r = lan.pairing().answer(m, from, in, out, () -> {
                    ScheduledFuture<?> brief = deadline.getAndSet(schedule(t, c, 160));
                    if (brief != null) brief.cancel(false);
                });
                case "delivery" -> r = arrival(m, from);
                default -> r = lan.reply(false, "lan.wire.unknown");
            }
            write(out, r);
        } catch (IOException | RuntimeException e) {
            // connection dropped or expired, or a malformed request: anyone on the network can send anything
        } finally {
            ScheduledFuture<?> f = deadline.get();
            if (f != null) f.cancel(false);
        }
    }

    private static ScheduledFuture<?> schedule(ScheduledExecutorService t, Socket c, long seconds) {
        try {
            return t != null ? t.schedule(() -> closeQuietly(c), seconds, TimeUnit.SECONDS) : null;
        } catch (RejectedExecutionException stopped) {
            return null;
        }
    }

    private static void closeQuietly(Socket c) {
        try {
            c.close();
        } catch (IOException e) {
            // already closed
        }
    }

    private JsonObject arrival(JsonObject m, String from) {
        String sender = text(m, "key");
        if (!lan.trusted(sender)) return lan.reply(false, "lan.wire.notLinked");
        String who = lan.trustedName(sender);
        Envelope.Keys keys = lan.keys();
        try {
            Envelope.Sealed b = new Envelope.Sealed(text(m, "ephemeral"), text(m, "iv"), text(m, "data"));
            JsonObject inside = JsonParser.parseString(Envelope.openFrom(b, keys, sender, sender, keys.publicKey())).getAsJsonObject();
            String id = text(inside, "id");
            long when = inside.get("when").getAsLong();
            // An old or already seen envelope is a replay: whoever listens to the network could send it again.
            if (Math.abs(System.currentTimeMillis() - when) > 10 * 60_000) return lan.reply(false, "lan.wire.tooOld");
            if (id == null || alreadyArrived.putIfAbsent(id, System.currentTimeMillis()) != null) return lan.reply(false, "lan.wire.already");
            Parcel p = GSON.fromJson(text(inside, "parcel"), Parcel.class);
            if (p == null || p.admit() != null) return lan.reply(false, "lan.wire.invalid");
            // The envelope opened with the sender's key: this is really where that PC is.
            lan.discovery().notice(m, from, false, true);
            lan.arrival(new Delivery("lan:" + id, who != null ? who : "?", sender, when, p, p.name, null));
            // The receipt is sealed with our key: whoever only announces our public key cannot write it.
            JsonObject r = lan.reply(true, null);
            Envelope.Sealed q = Envelope.sealFrom(id, keys, sender, keys.publicKey(), sender);
            JsonObject receipt = new JsonObject();
            receipt.addProperty("ephemeral", q.ephemeral());
            receipt.addProperty("iv", q.iv());
            receipt.addProperty("data", q.data());
            r.add("receipt", receipt);
            return r;
        } catch (GeneralSecurityException | RuntimeException e) {
            // not sealed for us, or fields missing or of the wrong type
            return lan.reply(false, "lan.wire.cannotOpen");
        }
    }
}
