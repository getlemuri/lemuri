package lemuri.fe.v;

import static org.junit.jupiter.api.Assertions.assertTrue;

import org.junit.jupiter.api.Test;
import lemuri.fe.m.Selection;

/** The window opens on the made-up world and a host can be chosen: the base of the other interface tests. */
class SmokeUiTest extends UiTest {
    @Test
    void theTreeShowsTheHostsAndASummaryOpens() {
        assertTrue(shows("web-01"));
        assertTrue(shows("Production"));
        robot.clickOn(text("db-01"));
        until(() -> app.state.node() instanceof Selection.HostItem h && h.hostId().equals("db-01"), "db-01 chosen");
        until(() -> shows("14 packages"), "updates in the summary");
        assertTrue(shows("required"));
        assertTrue(shows("3 security"));
        assertTrue(shows("/srv"));
    }
}
