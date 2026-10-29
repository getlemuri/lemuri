package lemuri.be.monitoring;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertThrows;

import org.junit.jupiter.api.Test;
import lemuri.be.dto.Resources;

/** Command outputs read by the metrics parser. */
class ParseTest {
    @Test
    void metrics() {
        Resources r = Metrics.parse(String.join("\n",
                "N 4",
                "C cpu  100 0 100 800 0 0 0 0 0 0",
                "C cpu  150 0 150 900 0 0 0 0 0 0",
                "M MemTotal: 4000000",
                "M MemAvailable: 1000000",
                "U 3600.5",
                "D 1000 400 500 /",
                "D 2000 100 1800 /data"));
        assertEquals(4, r.cpu());
        assertEquals(0.5, r.cpuUsage(), 1e-9);
        assertEquals(4_000_000, r.memTotalKb());
        assertEquals(3600, r.onForS());
        assertEquals("/", r.root().dot());
        assertEquals("/", r.fullest().dot());
    }

    @Test
    void metricsFromANonLinuxReply() {
        assertThrows(IllegalStateException.class, () -> Metrics.parse("Welcome!\nnothing here"));
    }
}
