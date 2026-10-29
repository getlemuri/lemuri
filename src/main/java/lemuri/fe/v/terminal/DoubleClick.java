package lemuri.fe.v.terminal;

import com.jcraft.jsch.ChannelSftp;
import com.jcraft.jsch.JSchException;
import com.jcraft.jsch.SftpException;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.function.Function;
import java.util.function.Supplier;
import javafx.application.Platform;
import lemuri.be.service.Transport;
import lemuri.be.terminal.FileCheck;
import lemuri.be.terminal.LocalTransport;
import lemuri.be.terminal.SftpFiles;
import lemuri.be.terminal.SshErrors;
import lemuri.be.terminal.SshTransport;
import lemuri.be.util.I18n;

/**
 * Interactive mode, the double click on a name listed by an ls at the current prompt: a folder is entered and listed, a text
 * file is opened with nano, a binary file or one over {@link FileCheck#MAX_TEXT} is not opened, and a note near
 * the mouse says why. What the name is, is asked before typing anything, to the server over SFTP on the same
 * connection (to this computer for a local shell), in the folder the ls listed if it was given one. The
 * command typed is only the one that applies, visible in the terminal as if typed by hand.
 */
final class DoubleClick {
    private DoubleClick() {}

    /** A name, where it was found, and how it is written in the command typed. */
    record Found(String path, String typed, FileCheck.Kind kind) {}

    static void onName(Terminal t, String name, String command, double screenX, double screenY) {
        if (!Terminal.typesCommands() || name.isEmpty() || !typable(name)) return;
        Transport tr = t.transport();
        if (!(tr instanceof SshTransport) && !(tr instanceof LocalTransport)) return;
        String listed = Terminal.lsFolder(command);
        t.currentFolder(shell -> {
            if (shell == null || !typable(shell)) return;
            Thread.ofVirtual().name("double-click").start(() -> {
                Found f;
                String failure = null;
                try {
                    f = find(tr, shell, listed, name);
                } catch (JSchException e) {
                    f = null;
                    failure = SshErrors.explain(e);
                }
                Found found = f;
                String why = failure;
                Platform.runLater(() -> act(t, found, name, why, screenX, screenY));
            });
        });
    }

    private static void act(Terminal t, Found f, String name, String failure, double x, double y) {
        String shown = "\"" + name + "\"";
        if (f == null) {
            t.hint(I18n.t("terminal.open.failed", shown, failure), x, y);
            return;
        }
        String q = quote(f.typed());
        switch (f.kind()) {
            case FOLDER -> t.typeCommand("cd " + q + " && ls");
            case TEXT -> t.typeCommand("nano " + q);
            case BINARY -> t.hint(I18n.t("terminal.open.binary", shown), x, y);
            case LARGE -> t.hint(I18n.t("terminal.open.large", shown), x, y);
            case OTHER -> t.hint(I18n.t("terminal.open.other", shown), x, y);
            case MISSING -> t.hint(I18n.t("terminal.open.missing", shown), x, y);
        }
    }

    /** The first place where the name is: in the folder the ls listed, then in the shell's folder. */
    private static Found find(Transport tr, String shell, String listed, String name) throws JSchException {
        if (tr instanceof SshTransport ssh) {
            ChannelSftp c = ssh.openSftp();
            try {
                return find(shell, listed, name, () -> home(c), p -> FileCheck.remote(c, p));
            } finally {
                c.disconnect();
            }
        }
        return find(shell, listed, name, () -> System.getProperty("user.home"), p -> FileCheck.local(Path.of(p)));
    }

    static Found find(String shell, String listed, String name, Supplier<String> home, Function<String, FileCheck.Kind> check) {
        Found last = null;
        for (Found f : places(shell, listed, name, home)) {
            if (!typable(f.path())) continue;
            FileCheck.Kind k = check.apply(f.path());
            last = new Found(f.path(), f.typed(), k);
            if (k != FileCheck.Kind.MISSING) return last;
        }
        return last != null ? last : new Found(name, name, FileCheck.Kind.MISSING);
    }

    /**
     * Where to look for the name. In the folder an ls was given ({@code ls /var/log}, {@code ls ~/backups},
     * {@code ls app}), typed with its full path; then in the shell's folder, typed as it is. A name that starts
     * with "-" gets "./" in front, otherwise nano and cd would take it as an option ("cd -" goes back).
     */
    static List<Found> places(String shell, String listed, String name, Supplier<String> home) {
        List<Found> out = new ArrayList<>();
        if (listed != null) {
            String base = listed.equals("~") ? home.get() : listed.startsWith("~/") ? SftpFiles.join(home.get(), listed.substring(2))
                    : listed.startsWith("/") ? listed : SftpFiles.join(shell, listed);
            String p = SftpFiles.join(base, name);
            out.add(new Found(p, p, null));
        }
        out.add(new Found(SftpFiles.join(shell, name), name.startsWith("-") ? "./" + name : name, null));
        return out;
    }

    private static String home(ChannelSftp c) {
        try {
            return c.getHome();
        } catch (SftpException e) {
            return "/";
        }
    }

    /**
     * Names with "\" or control characters are not typed: inside quotes fish reads \' as a quote, so the name
     * could end the string and the rest would run as a command.
     */
    static boolean typable(String s) {
        return s.indexOf('\\') < 0 && s.chars().noneMatch(Character::isISOControl);
    }

    /**
     * The name as the user would type it: as it is if made only of letters, digits, ".", "_", "-" and "/" (and not
     * starting with "-"), which no shell reads in another way; otherwise in single quotes.
     */
    static String quote(String s) {
        if (s.matches("[\\p{L}\\p{N}._/][\\p{L}\\p{N}._/-]*")) return s;
        return "'" + s.replace("'", "'\\''") + "'";
    }
}
