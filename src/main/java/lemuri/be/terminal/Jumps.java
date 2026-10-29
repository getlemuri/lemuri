package lemuri.be.terminal;

import java.util.ArrayList;
import java.util.Collection;
import java.util.List;
import lemuri.be.dto.Host;
import lemuri.be.util.I18n;

/**
 * Jump hosts (ssh's ProxyJump): from the text of {@link Host#jump} to the hosts to go through, in order.
 * A hop is the name of another host in the list, with its user, port, key and saved password (and its own
 * jump hosts before it), or {@code [user@]host[:port]}, reached with ssh-agent and the keys in ~/.ssh.
 */
public final class Jumps {
    /** A chain longer than this is a loop. */
    private static final int MAX_HOPS = 8;

    private Jumps() {}

    /** A copy of {@code h} with {@link Host#route} (or {@link Host#routeError}) filled in from {@code known}. */
    public static Host resolve(Host h, Collection<Host> known) {
        Host c = h.copy();
        c.route = null;
        c.routeError = null;
        if (h.jump == null || h.jump.isBlank()) return c;
        try {
            List<Host> route = new ArrayList<>();
            add(h.jump, known, route, List.of(h.name), 0);
            c.route = List.copyOf(route);
        } catch (IllegalArgumentException e) {
            c.routeError = e.getMessage();
        }
        return c;
    }

    private static void add(String spec, Collection<Host> known, List<Host> route, List<String> visiting, int depth) {
        for (String raw : spec.split(",", -1)) {
            String token = raw.strip();
            if (token.isEmpty()) throw new IllegalArgumentException(I18n.t("jump.err.empty"));
            if (route.size() >= MAX_HOPS || depth > MAX_HOPS) throw new IllegalArgumentException(I18n.t("jump.err.tooMany", String.valueOf(MAX_HOPS)));
            Host named = known.stream().filter(x -> token.equalsIgnoreCase(x.name)).findFirst().orElse(null);
            if (named != null) {
                if (visiting.stream().anyMatch(token::equalsIgnoreCase)) throw new IllegalArgumentException(I18n.t("jump.err.loop", token));
                // A host in the list may itself be behind other jump hosts: those come first.
                if (named.jump != null && !named.jump.isBlank()) {
                    List<String> deeper = new ArrayList<>(visiting);
                    deeper.add(named.name);
                    add(named.jump, known, route, deeper, depth + 1);
                }
                Host hop = named.copy();
                hop.jump = null;
                hop.route = null;
                route.add(hop);
            } else {
                route.add(parse(token));
            }
        }
    }

    /** {@code [user@]host[:port]}, with IPv6 written as {@code [::1]:22}. */
    static Host parse(String token) {
        String user = System.getProperty("user.name"), rest = token;
        int at = rest.lastIndexOf('@');
        if (at >= 0) {
            user = rest.substring(0, at);
            rest = rest.substring(at + 1);
        }
        String address = rest;
        int port = 22;
        if (rest.startsWith("[")) {
            int close = rest.indexOf(']');
            if (close < 0) throw new IllegalArgumentException(I18n.t("jump.err.invalid", token));
            address = rest.substring(1, close);
            if (rest.length() > close + 1) port = port(rest.substring(close + 1), token);
        } else if (rest.indexOf(':') >= 0 && rest.indexOf(':') == rest.lastIndexOf(':')) {
            address = rest.substring(0, rest.indexOf(':'));
            port = port(rest.substring(rest.indexOf(':')), token);
        }
        if (user.isEmpty() || address.isEmpty() || !address.matches("[^\\s/@]+")) throw new IllegalArgumentException(I18n.t("jump.err.invalid", token));
        Host h = new Host();
        h.name = token;
        h.user = user;
        h.address = address;
        h.port = port;
        h.authMethod = Host.AuthMethod.AGENT;
        return h;
    }

    private static int port(String colonPort, String token) {
        try {
            if (!colonPort.startsWith(":")) throw new NumberFormatException();
            int p = Integer.parseInt(colonPort.substring(1));
            if (p < 1 || p > 65535) throw new NumberFormatException();
            return p;
        } catch (NumberFormatException e) {
            throw new IllegalArgumentException(I18n.t("jump.err.invalid", token));
        }
    }
}
