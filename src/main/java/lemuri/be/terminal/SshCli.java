package lemuri.be.terminal;

import java.util.ArrayList;
import java.util.List;
import java.util.regex.Pattern;
import lemuri.be.dto.Host;
import lemuri.be.util.I18n;

/**
 * The OpenSSH configuration for {@code lemuri -s HOST}: the host and each jump host before it, with its own
 * address, port, user and key, so that the system's ssh opens the same connection the app would, in the
 * terminal. Host keys are the same too: the app keeps them in ~/.ssh/known_hosts.
 * Every value ends up in a configuration file and hosts can arrive from contacts: anything that could
 * add a line or an option (spaces, quotes, control characters) is refused.
 */
public final class SshCli {
    /** The name ssh is given for the host; the jump hosts are {@value #HOP}1, 2… */
    public static final String TARGET = "lemuri-target";
    private static final String HOP = "lemuri-hop-";
    private static final Pattern ADDRESS = Pattern.compile("[A-Za-z0-9._:-]+");
    private static final Pattern USER = Pattern.compile("[A-Za-z0-9._][A-Za-z0-9._-]*");

    private SshCli() {}

    /** {@code h} with its route already resolved ({@link Jumps#resolve}). */
    public static String config(Host h) {
        if (h.routeError != null) throw new IllegalArgumentException(h.routeError);
        StringBuilder b = new StringBuilder("# Written by Lemuri for \"lemuri -s\": rewritten every time.\n");
        List<Host> route = h.route != null ? h.route : List.of();
        for (int i = 0; i < route.size(); i++) block(b, HOP + (i + 1), route.get(i), i == 0 ? null : HOP + i);
        block(b, TARGET, h, route.isEmpty() ? null : HOP + route.size());
        // The user's own settings (ServerAliveInterval, ciphers…) still apply where these blocks say nothing.
        b.append("Match all\nInclude ~/.ssh/config\n");
        return b.toString();
    }

    /** The command to run: ssh with this configuration, then what the user wrote after the host name. */
    public static List<String> command(String configFile, List<String> more) {
        List<String> c = new ArrayList<>(List.of("ssh", "-F", configFile, TARGET));
        c.addAll(more);
        return c;
    }

    private static void block(StringBuilder b, String alias, Host h, String through) {
        if (h.address == null || !ADDRESS.matcher(h.address).matches() || h.address.startsWith("-"))
            throw new IllegalArgumentException(I18n.t("cli.err.address", h.name, String.valueOf(h.address)));
        if (h.user == null || !USER.matcher(h.user).matches())
            throw new IllegalArgumentException(I18n.t("cli.err.user", h.name, String.valueOf(h.user)));
        if (h.port < 1 || h.port > 65535) throw new IllegalArgumentException(I18n.t("cli.err.port", h.name, String.valueOf(h.port)));
        b.append("Host ").append(alias).append('\n')
                .append("    HostName ").append(h.address).append('\n')
                .append("    Port ").append(h.port).append('\n')
                .append("    User ").append(h.user).append('\n');
        if (h.authMethod == Host.AuthMethod.KEY && h.key != null && !h.key.isBlank()) {
            if (h.key.chars().anyMatch(ch -> ch < 0x20 || ch == 0x7f || ch == '"'))
                throw new IllegalArgumentException(I18n.t("cli.err.key", h.name));
            // In IdentityFile "%" starts a token (%d, %u…): written twice it is a plain %.
            b.append("    IdentityFile \"").append(h.key.replace("%", "%%")).append("\"\n")
                    .append("    IdentitiesOnly yes\n");
        }
        if (through != null) b.append("    ProxyJump ").append(through).append('\n');
    }
}
