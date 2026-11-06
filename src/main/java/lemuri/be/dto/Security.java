package lemuri.be.dto;

import java.util.ArrayList;
import java.util.List;

/**
 * How safe the SSH access to a host is: one check per aspect, from the connection itself (algorithms, keys,
 * version) and from the server (sshd settings, brute force protection, updates). Only read, nothing is changed.
 * {@code error} is set when the server could not be read; the checks known anyway are still there.
 */
public record Security(List<Check> checks, String error, long when) {
    public enum Level { OK, WARN, BAD }

    /**
     * One aspect. {@code id} names its texts (security.&lt;id&gt; and security.&lt;id&gt;.advice); {@code value} is
     * what was found, as it is (an algorithm, a setting); {@code fix} what to do, or null: a command to run on
     * this computer for the client.* checks, a line of sshd_config for the others. {@code weight}
     * is how much it counts in the score.
     */
    public record Check(String id, Level level, String value, String fix, int weight) {}

    public Security {
        checks = List.copyOf(checks);
    }

    /** 0-1, from the checks: OK counts in full, WARN half, BAD nothing. Null without checks. */
    public Double score() {
        double got = 0, all = 0;
        for (Check c : checks) {
            all += c.weight();
            got += c.weight() * (c.level() == Level.OK ? 1 : c.level() == Level.WARN ? 0.5 : 0);
        }
        return all == 0 ? null : got / all;
    }

    /** With the security updates check, from the count the summary already has; as it is if unknown. */
    public Security withUpdates(Updates u) {
        if (u == null || u.error() != null || u.security() == null) return this;
        return with(new Check("server.updates", u.security() > 0 ? Level.BAD : Level.OK, String.valueOf(u.security()), null, 2));
    }

    /** The same report with {@code c} in place of the check with its id, or added at the end. */
    public Security with(Check c) {
        List<Check> l = new ArrayList<>(checks);
        l.removeIf(x -> x.id().equals(c.id()));
        l.add(c);
        return new Security(l, error, when);
    }
}
