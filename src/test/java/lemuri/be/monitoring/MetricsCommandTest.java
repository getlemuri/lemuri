package lemuri.be.monitoring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertThrows;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.ByteArrayInputStream;
import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.util.concurrent.TimeUnit;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.condition.EnabledOnOs;
import org.junit.jupiter.api.condition.OS;
import lemuri.be.dto.Resources;
import lemuri.be.dto.Updates;
import lemuri.be.terminal.SshTransport;

/** The metrics command, really run by a Linux shell as the server would. */
class MetricsCommandTest {
    @Test
    @EnabledOnOs(OS.LINUX)
    void commandRunsAndGivesValidResources() throws Exception {
        Process p = new ProcessBuilder("sh", "-c", Metrics.COMMAND).redirectErrorStream(true).start();
        assertTrue(p.waitFor(20, TimeUnit.SECONDS), "the command must finish");
        String output = new String(p.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        Resources r = Metrics.parse(output);
        assertTrue(r.cpu() > 0, output);
        assertTrue(r.memTotalKb() > 0, output);
        assertFalse(r.disks().isEmpty(), output);
    }

    @Test
    @EnabledOnOs(OS.LINUX)
    void updatesCommandRuns() throws Exception {
        Process p = new ProcessBuilder("sh", "-c", Metrics.UPDATES).redirectErrorStream(true).start();
        assertTrue(p.waitFor(60, TimeUnit.SECONDS), "the command must finish");
        String output = new String(p.getInputStream().readAllBytes(), StandardCharsets.UTF_8);
        Updates u = Metrics.parseUpdates(output, 0);
        assertTrue(u.error() == null, output);
    }

    /** Analysis #14, 2.4: the reason for a reboot is written by the server and shown in a label. */
    @Test
    void theReasonForARebootIsCleanedAndCut() {
        Updates u = Metrics.parseUpdates("R yes linux\u202Eevil\u200B\u0007 base " + "x".repeat(4_000_000) + "\n", 0);
        assertTrue(u.rebootBecause().startsWith("linuxevil base x"), u.rebootBecause().substring(0, 20));
        assertTrue(u.rebootBecause().length() <= Metrics.MAX_BECAUSE + 1);
        assertTrue(u.rebootBecause().endsWith("…"));
    }

    @Test
    void updatesAreRead() {
        Updates apt = Metrics.parseUpdates("A 12;3\nR yes linux-image-6.8.0-50 linux-base \n", 0);
        assertEquals(12, apt.packages());
        assertEquals(3, apt.security());
        assertEquals(Boolean.TRUE, apt.reboot());
        assertEquals("linux-image-6.8.0-50 linux-base", apt.rebootBecause());
        Updates dnf = Metrics.parseUpdates("A 4;\nR no\n", 0);
        assertEquals(4, dnf.packages());
        assertEquals(null, dnf.security());
        assertEquals(Boolean.FALSE, dnf.reboot());
        Updates nothing = Metrics.parseUpdates("welcome\n", 0);
        assertEquals(null, nothing.packages());
        assertEquals(null, nothing.reboot());
    }

    @Test
    void tooLongOutputIsRejected() throws Exception {
        byte[] little = "ok".getBytes(StandardCharsets.UTF_8);
        assertEquals("ok", SshTransport.readAtMost(new ByteArrayInputStream(little), 10));
        byte[] much = new byte[100_000];
        assertThrows(IOException.class, () -> SshTransport.readAtMost(new ByteArrayInputStream(much), 64 * 1024));
    }
}
