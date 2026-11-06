package lemuri.fe.m;

import java.io.File;
import lemuri.be.dto.Host;
import lemuri.be.util.Os;

/**
 * This computer, as the host of the local shell tabs: never saved and never in the host list, so the tree,
 * the metrics and the sharing do not see it. The console finds it through {@link AppState#host(String)}.
 */
public final class LocalHost extends Host {
    public static final String ID = "local";
    public static final LocalHost INSTANCE = new LocalHost();

    private LocalHost() {
        id = ID;
        String own = System.getenv("SHELL");
        name = own != null && !own.isBlank() ? new File(own).getName() : Os.MAC ? "zsh" : "bash";
        user = System.getProperty("user.name");
        address = "localhost";
    }

    public static boolean is(Host h) {
        return h != null && ID.equals(h.id);
    }

    @Override
    public String destination() {
        return user + "@" + address;
    }
}
