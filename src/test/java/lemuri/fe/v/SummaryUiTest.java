package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.util.ArrayList;
import java.util.List;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;
import lemuri.be.dto.HostStatus;
import lemuri.be.dto.Updates;

/** The host summary: resources, disks and every case of pending updates and reboot. */
class SummaryUiTest extends UiTest {
    @Override
    protected List<Host> hosts() {
        List<Host> l = new ArrayList<>(super.hosts());
        Host off = host("old-box", "Personal", "192.0.2.99");
        off.state = HostStatus.OFFLINE;
        l.add(off);
        return l;
    }

    private void open(String name, Updates u) {
        metrics.updates = u;
        robot.clickOn(text(name));
        until(() -> "summary".equals(String.valueOf(app.state.position.get().item())) || app.state.position.get().item() == null, "summary");
        settle();
    }

    @Test
    void updatesAndARequiredReboot() {
        open("db-01", new Updates(14, 3, true, "linux-image-6.8.0-51-generic", null, 0));
        until(() -> shows("14 packages"), "updates");
        assertTrue(shows("3 security"));
        assertTrue(shows("required"));
        assertTrue(shows("linux-image-6.8.0-51-generic"));
        // Resources and disks are there too, and the full disk is coloured as critical.
        assertTrue(shows("4 vCPU"));
        assertTrue(shows("/srv"));
        assertTrue(!all(".usage-bar-value.critical").isEmpty(), "the 95% disk is shown as critical");
    }

    @Test
    void upToDateWithoutReboot() {
        open("web-01", new Updates(0, 0, false, null, null, 0));
        until(() -> shows("up to date"), "up to date");
        assertTrue(shows("not needed"));
        assertFalse(shows("security"));
    }

    @Test
    void anUnknownSystemSaysSo() {
        open("web-01", new Updates(null, null, null, null, null, 0));
        until(() -> shows("no known package manager"), "unknown manager");
        assertTrue(shows("cannot tell"));
    }

    @Test
    void anErrorIsShownInsteadOfNumbers() {
        open("web-01", new Updates(null, null, null, null, "authentication refused", 0));
        until(() -> shows("authentication refused"), "the error");
        assertFalse(shows("packages"));
    }

    @Test
    void anUnreachableHostAsksNothing() {
        int before = metrics.refreshed.size();
        robot.clickOn(text("old-box"));
        settle();
        until(() -> shows("host unreachable"), "unreachable");
        assertTrue(metrics.refreshed.subList(before, metrics.refreshed.size()).stream().noneMatch("old-box"::equals), "no reading of an offline host");
    }
}
