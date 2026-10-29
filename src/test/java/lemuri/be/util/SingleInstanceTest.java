package lemuri.be.util;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.nio.file.Path;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.io.TempDir;

/** Only one copy of the app: the second asks the first to show itself and does not start. */
class SingleInstanceTest {
    @Test
    void secondCopyCallsTheFirstAndDoesNotStart(@TempDir Path folder) throws Exception {
        CountDownLatch call = new CountDownLatch(1);
        SingleInstance before = SingleInstance.acquire(folder, call::countDown);
        assertNotNull(before);
        try {
            assertNull(SingleInstance.acquire(folder, () -> {}), "a second copy must not start");
            assertTrue(call.await(5, TimeUnit.SECONDS), "the first copy must be asked to show itself");
        } finally {
            before.release();
        }
        // Once the first has quit, a new copy can start.
        SingleInstance after = SingleInstance.acquire(folder, () -> {});
        assertNotNull(after);
        after.release();
    }

    @Test
    void toggleReachesTheFirstCopy(@TempDir Path folder) throws Exception {
        BlockingQueue<String> asked = new LinkedBlockingQueue<>();
        SingleInstance before = SingleInstance.acquire(folder, SingleInstance.SHOW, asked::add);
        assertNotNull(before);
        try {
            assertNull(SingleInstance.acquire(folder, SingleInstance.TOGGLE, c -> {}));
            assertEquals(SingleInstance.TOGGLE, asked.poll(5, TimeUnit.SECONDS));
            assertNull(SingleInstance.acquire(folder, () -> {}));
            assertEquals(SingleInstance.SHOW, asked.poll(5, TimeUnit.SECONDS));
        } finally {
            before.release();
        }
    }
}
