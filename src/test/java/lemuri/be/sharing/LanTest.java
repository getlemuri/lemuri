package lemuri.be.sharing;

import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.net.InetAddress;
import org.junit.jupiter.api.Test;

class LanTest {
    private static boolean isPrivate(String a) throws Exception {
        return LanWire.isPrivate(InetAddress.getByName(a));
    }

    @Test
    void answersOnlyFromTheLocalNetwork() throws Exception {
        for (String a : new String[] {"10.1.2.3", "172.16.0.1", "172.31.255.255", "192.168.1.20", "127.0.0.1", "169.254.1.1",
                "100.64.0.1", "100.127.255.255", "::1", "fe80::1", "fd12:3456::1"})
            assertTrue(isPrivate(a), a);
        for (String a : new String[] {"8.8.8.8", "172.32.0.1", "100.128.0.1", "203.0.113.10", "2001:db8::1"})
            assertFalse(isPrivate(a), a);
    }
}
