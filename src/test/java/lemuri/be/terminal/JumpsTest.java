package lemuri.be.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotNull;
import static org.junit.jupiter.api.Assertions.assertNull;

import java.util.List;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.Host;

/** From the text of ProxyJump to the hosts to go through. */
class JumpsTest {
    private static Host host(String name, String jump) {
        Host h = new Host();
        h.id = name;
        h.name = name;
        h.address = name + ".example.com";
        h.user = "ops";
        h.jump = jump;
        return h;
    }

    private static List<String> route(Host h) {
        return h.route.stream().map(Host::destination).toList();
    }

    @Test
    void noJumpMeansADirectConnection() {
        Host r = Jumps.resolve(host("web", null), List.of());
        assertNull(r.route);
        assertNull(r.routeError);
        assertNull(Jumps.resolve(host("web", "  "), List.of()).route);
    }

    @Test
    void namesOfTheListAndLiteralHostsInOrder() {
        Host bastion = host("Bastion", null);
        bastion.port = 2222;
        Host r = Jumps.resolve(host("db", "bastion, admin@10.0.0.5:2200, [fd00::1]:22, inner"), List.of(bastion));
        assertEquals(List.of("ops@Bastion.example.com:2222", "admin@10.0.0.5:2200", System.getProperty("user.name") + "@fd00::1:22",
                System.getProperty("user.name") + "@inner:22"), route(r));
        // A hop from the list keeps its id, so its saved password is found; a literal one has none.
        assertEquals("Bastion", r.route.get(0).id);
        assertNull(r.route.get(1).id);
        assertEquals(Host.AuthMethod.AGENT, r.route.get(1).authMethod);
    }

    @Test
    void aJumpHostBehindAnotherComesAfterIt() {
        Host outer = host("outer", null), inner = host("inner", "outer");
        Host r = Jumps.resolve(host("db", "inner"), List.of(outer, inner));
        assertEquals(List.of("ops@outer.example.com:22", "ops@inner.example.com:22"), route(r));
        assertNull(r.route.get(1).jump);
    }

    @Test
    void loopsAndMistakesBecomeAnError() {
        Host a = host("a", "b"), b = host("b", "a");
        assertNotNull(Jumps.resolve(a, List.of(a, b)).routeError);
        assertNotNull(Jumps.resolve(host("web", "web"), List.of(host("web", "web"))).routeError);
        assertNotNull(Jumps.resolve(host("web", "bastion,,x"), List.of()).routeError);
        assertNotNull(Jumps.resolve(host("web", "bastion,"), List.of()).routeError);
        assertNotNull(Jumps.resolve(host("web", "user@host:99999"), List.of()).routeError);
        assertNotNull(Jumps.resolve(host("web", "@host"), List.of()).routeError);
        assertNull(Jumps.resolve(host("web", "user@host:22"), List.of()).routeError);
    }
}
