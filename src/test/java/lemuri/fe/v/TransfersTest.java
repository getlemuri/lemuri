package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.List;
import java.util.concurrent.CompletableFuture;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import org.junit.jupiter.api.Test;

/** Transfers: how many run at once on a host, the summary at the end. */
class TransfersTest {
    /** Up to four at once on a host: the fifth waits for one of them, another host does not wait at all. */
    @Test
    void fourAtOnceOnAHostTheFifthWaits() throws Exception {
        Transfers.Slots slots = new Transfers.Slots(Transfers.PER_HOST);
        for (int i = 0; i < Transfers.PER_HOST; i++) assertTrue(slots.take("web-01", () -> false));
        assertTrue(slots.take("db-01", () -> false), "another host has places of its own");
        CompletableFuture<Boolean> fifth = CompletableFuture.supplyAsync(() -> {
            try {
                return slots.take("web-01", () -> false);
            } catch (InterruptedException e) {
                return false;
            }
        });
        Thread.sleep(500);
        assertFalse(fifth.isDone(), "the fifth waits");
        slots.give("web-01");
        assertTrue(fifth.get(5, TimeUnit.SECONDS), "and starts when one ends");
    }

    /** Cancelled while it waits: it stops waiting and takes no place. */
    @Test
    void aWaitingTransferCanBeCancelled() throws Exception {
        Transfers.Slots slots = new Transfers.Slots(1);
        assertTrue(slots.take("web-01", () -> false));
        AtomicBoolean cancelled = new AtomicBoolean();
        CompletableFuture<Boolean> waiting = CompletableFuture.supplyAsync(() -> {
            try {
                return slots.take("web-01", cancelled::get);
            } catch (InterruptedException e) {
                return true;
            }
        });
        cancelled.set(true);
        assertFalse(waiting.get(5, TimeUnit.SECONDS));
        slots.give("web-01");
        assertTrue(slots.take("web-01", () -> false), "the place is free again");
    }

    /** One transfer says what it did; several are counted, and what failed is named with its reason. */
    @Test
    void theSummaryNamesWhatFailed() {
        Transfers.Outcome ok = new Transfers.Outcome("\"a.txt\"", Transfers.Kind.DONE, "Uploaded \"a.txt\" to /tmp", null);
        Transfers.Outcome bad = new Transfers.Outcome("\"b.iso\"", Transfers.Kind.FAILED, "Transfer failed: no space", "no space");
        Transfers.Outcome stopped = new Transfers.Outcome("\"c.log\"", Transfers.Kind.CANCELLED, "Upload cancelled", null);
        assertEquals("Uploaded \"a.txt\" to /tmp", Transfers.summary(List.of(ok)));
        assertEquals("Done: 2 · Failed: 1 · Cancelled: 1 · \"b.iso\": no space", Transfers.summary(List.of(ok, bad, ok, stopped)));
        assertEquals("Done: 2", Transfers.summary(List.of(ok, ok)));
    }
}
