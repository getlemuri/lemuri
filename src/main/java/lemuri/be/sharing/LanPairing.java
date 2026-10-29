package lemuri.be.sharing;

import static lemuri.be.sharing.LanWire.read;
import static lemuri.be.sharing.LanWire.text;
import static lemuri.be.sharing.LanWire.write;

import com.google.gson.JsonObject;
import java.io.BufferedInputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.util.Map;
import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.ExecutionException;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.TimeoutException;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.function.Consumer;
import lemuri.be.dto.Peer;
import lemuri.be.dto.Request;
import lemuri.be.util.I18n;

/**
 * Pairing two PCs with the 6-digit code, on both sides. Whoever receives the request commits to its
 * random number before seeing the other one, so a man in the middle cannot choose the numbers to make
 * the same code appear on both screens (see {@link Envelope#commitment}).
 */
final class LanPairing {
    /** Pairing version: from 2 on, the code is computed with the commitment. */
    static final int VERSION = 2;
    /** From the same address, at most one pairing request in this time. */
    static final long PAUSE_MS = 60_000;

    private final Lan lan;
    /** address -> last pairing request: whoever insists does not make dialogs pop up in a burst. */
    private final Map<String, Long> lastRequests = new ConcurrentHashMap<>();
    private final AtomicBoolean busy = new AtomicBoolean();

    LanPairing(Lan lan) {
        this.lan = lan;
    }

    /**
     * Asks {@code p} to pair, on the same connection: it commits to its random number, we send ours,
     * it reveals its own and we check the commitment. Only then is the code computed and given to
     * {@code onCode}; then we wait for the choice of the user over there. True if they accepted.
     * Blocking: call it from a worker thread.
     */
    boolean ask(Peer p, Consumer<String> onCode) throws IOException {
        Envelope.Keys keys = lan.keys();
        try (Socket s = new Socket()) {
            s.connect(new InetSocketAddress(p.address(), p.port()), 3000);
            s.setSoTimeout(15_000);
            InputStream in = new BufferedInputStream(s.getInputStream());
            OutputStream out = s.getOutputStream();
            JsonObject request = lan.identity("pair");
            request.addProperty("v", VERSION);
            write(out, request);
            JsonObject r = read(in);
            if (!"commitment".equals(text(r, "type"))) {
                String reason = text(r, "reason");
                throw new IllegalStateException(reason != null ? I18n.ifKey(reason) : I18n.t("lan.wire.older"));
            }
            if (!p.key().equals(text(r, "key"))) throw new IllegalStateException(I18n.t("lan.differentKey"));
            String commitment = text(r, "commitment");
            String mine = Envelope.nonce();
            JsonObject n = new JsonObject();
            n.addProperty("nonce", mine);
            write(out, n);
            String theirs = text(read(in), "nonce");
            if (theirs == null || commitment == null || !Envelope.commitment(p.key(), keys.publicKey(), theirs).equals(commitment))
                throw new IllegalStateException(I18n.t("lan.brokenProtocol"));
            onCode.accept(Envelope.code(keys.publicKey(), p.key(), mine, theirs));
            // Now wait for the user over there to compare the code.
            s.setSoTimeout(130_000);
            JsonObject end = read(in);
            if (!LanWire.yes(end, "ok")) {
                String reason = text(end, "reason");
                if (reason != null && !reason.equals("lan.wire.rejected")) throw new IllegalStateException(I18n.ifKey(reason));
                return false;
            }
            return true;
        }
    }

    /**
     * Answers a pairing request: commitment to our random number, the other's number, reveal of ours;
     * then the code, the same as the one shown over there, and the user's choice. {@code extend} is called
     * when the numbers have been exchanged and we move on to waiting for the user.
     */
    JsonObject answer(JsonObject m, String from, InputStream in, OutputStream out, Runnable extend) throws IOException {
        Peer p = lan.discovery().notice(m, from, false);
        if (p == null) return lan.reply(false, "lan.wire.invalid");
        if (LanWire.number(m, "v", 0) < VERSION)
            return lan.reply(false, "lan.wire.older");
        long now = System.currentTimeMillis();
        Long last = lastRequests.put(from, now);
        if (last != null && now - last < PAUSE_MS)
            return lan.reply(false, "lan.wire.tooMany");
        if (!busy.compareAndSet(false, true)) return lan.reply(false, "lan.wire.busy");
        try {
            Envelope.Keys keys = lan.keys();
            String mine = Envelope.nonce();
            JsonObject commitment = lan.identity("commitment");
            commitment.addProperty("commitment", Envelope.commitment(keys.publicKey(), p.key(), mine));
            write(out, commitment);
            String theirs = text(read(in), "nonce");
            if (theirs == null) return lan.reply(false, "lan.wire.invalid");
            JsonObject reveal = new JsonObject();
            reveal.addProperty("nonce", mine);
            write(out, reveal);
            extend.run();
            Request rec = new Request(p, Envelope.code(p.key(), keys.publicKey(), theirs, mine));
            boolean yes;
            try {
                String name = lan.askUser(rec).get(120, TimeUnit.SECONDS);
                yes = name != null;
                if (yes) lan.trusted(p.key(), name);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                yes = false;
            } catch (ExecutionException | TimeoutException | IOException | IllegalArgumentException | IllegalStateException e) {
                // no answer in time, or the contact could not be saved (name taken, vault locked)
                yes = false;
            }
            return lan.reply(yes, yes ? null : "lan.wire.rejected");
        } finally {
            busy.set(false);
        }
    }

    /** Forgets the requests older than the pause. */
    void prune(long now) {
        lastRequests.values().removeIf(t -> now - t > PAUSE_MS);
    }
}
