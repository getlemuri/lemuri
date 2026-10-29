package lemuri.be.sharing;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.concurrent.TimeUnit;
import lemuri.be.util.I18n;
import lemuri.be.util.Os;

/** The firewall of this PC, which may keep the other PCs of the local network out. */
final class LanFirewall {
    private LanFirewall() {}

    /**
     * The firewall active on this PC, if one is found: firewalld (active service) or ufw
     * (ENABLED=yes in /etc/ufw/ufw.conf). Reading the rules would need root: we only say it is there.
     */
    static String active() {
        if (Os.MAC) return null;
        try {
            Process p = new ProcessBuilder("systemctl", "is-active", "--quiet", "firewalld").redirectErrorStream(true).start();
            if (p.waitFor(3, TimeUnit.SECONDS) && p.exitValue() == 0) return "firewalld";
        } catch (IOException e) {
            // without systemctl only ufw is checked
        } catch (InterruptedException e) {
            Thread.currentThread().interrupt();
        }
        try {
            if (Files.readString(Path.of("/etc/ufw/ufw.conf")).lines().anyMatch(r -> r.trim().equals("ENABLED=yes")))
                return "ufw";
        } catch (IOException e) {
            // no ufw
        }
        return null;
    }

    /** What to tell the user: which ports to open, with the command for their firewall. */
    static String howToOpen(String firewall) {
        String command = firewall.equals("ufw")
                ? "sudo ufw allow " + Lan.ANNOUNCE_PORT + "/udp && sudo ufw allow " + Lan.PORT + "/tcp"
                : "sudo firewall-cmd --permanent --add-port=" + Lan.ANNOUNCE_PORT + "/udp --add-port=" + Lan.PORT + "/tcp && sudo firewall-cmd --reload";
        return I18n.t("lan.firewallHere", firewall, Lan.ANNOUNCE_PORT, Lan.PORT, command);
    }
}
