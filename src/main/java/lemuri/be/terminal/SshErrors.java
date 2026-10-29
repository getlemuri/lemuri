package lemuri.be.terminal;

import com.jcraft.jsch.JSchException;
import java.net.ConnectException;
import java.net.NoRouteToHostException;
import java.net.SocketTimeoutException;
import java.net.UnknownHostException;
import java.util.Locale;
import lemuri.be.util.I18n;

/**
 * From the exceptions of JSch and of the network to a sentence for the user: what happened and what to
 * check. JSch says most things only in the text of its messages, so those are matched as well as the types.
 */
public final class SshErrors {
    private SshErrors() {}

    /** A failure already explained (a jump host): its message is shown as it is. */
    public static final class Explained extends JSchException {
        public Explained(String message, Throwable cause) {
            super(message, cause);
        }
    }

    /** The sentence for the user. */
    public static String explain(Throwable e) {
        if (e instanceof Explained) return e.getMessage();
        String key = key(e);
        return key != null ? I18n.t(key) : technical(e);
    }

    /** The key of the sentence in the messages, or null if the error is not one we know. */
    public static String key(Throwable e) {
        for (Throwable c = e; c != null; c = c.getCause()) {
            if (c instanceof UnknownHostException) return "ssh.err.unknownHost";
            if (c instanceof NoRouteToHostException) return "ssh.err.noRoute";
            if (c instanceof ConnectException) return String.valueOf(c.getMessage()).contains("timed out") ? "ssh.err.timeout" : "ssh.err.refused";
            if (c instanceof SocketTimeoutException) return "ssh.err.timeout";
            if (c.getCause() == c) break;
        }
        String m = text(e);
        if (m.contains("timeout: socket is not established") || m.contains("timed out")) return "ssh.err.timeout";
        if (m.contains("connection refused")) return "ssh.err.refused";
        if (m.contains("unknownhostexception")) return "ssh.err.unknownHost";
        if (m.contains("reject hostkey") || m.contains("hostkey has been changed")) return "ssh.err.hostKey";
        if (m.contains("auth fail") || m.contains("auth cancel") || m.contains("too many authentication failures")) return "ssh.err.auth";
        if (m.contains("invalid privatekey")) return "ssh.err.key";
        if (m.contains("algorithm negotiation fail")) return "ssh.err.algorithms";
        if (m.contains("connection reset") || m.contains("end of io stream") || m.contains("connection is closed by foreign host")
                || m.contains("broken pipe")) return "ssh.err.closedByServer";
        if (m.contains("session is down") || m.contains("channel is not opened") || m.contains("packet corrupt")) return "ssh.err.dropped";
        return null;
    }

    /** The original message, for the details. */
    public static String technical(Throwable e) {
        return e.getMessage() != null ? e.getMessage() : e.toString();
    }

    /** All the messages of the chain, in lower case. */
    private static String text(Throwable e) {
        StringBuilder b = new StringBuilder();
        for (Throwable c = e; c != null && b.length() < 4096; c = c.getCause()) {
            b.append(c).append('\n');
            if (c.getCause() == c) break;
        }
        return b.toString().toLowerCase(Locale.ROOT);
    }
}
