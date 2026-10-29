package lemuri.be.util;

import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.attribute.PosixFilePermissions;
import java.time.LocalDateTime;
import java.time.ZoneId;
import java.time.format.DateTimeFormatter;
import java.util.logging.ConsoleHandler;
import java.util.logging.FileHandler;
import java.util.logging.Formatter;
import java.util.logging.Level;
import java.util.logging.LogRecord;
import java.util.logging.Logger;

/**
 * The app's technical log (not the task log at the bottom): ~/.lemuri/logs/lemuri-0.log,
 * three rotating 1 MB files, to attach to bug reports. It uses java.util.logging, where the
 * libraries writing through SLF4J end up too. Never passwords, tokens or keys in the messages.
 */
public final class Log {
    private static final Path DIR = Path.of(System.getProperty("user.home"), ".lemuri", "logs");
    private static final DateTimeFormatter TIME = DateTimeFormatter.ofPattern("yyyy-MM-dd HH:mm:ss");

    private Log() {}

    public static Logger of(Class<?> c) {
        return Logger.getLogger(c.getName());
    }

    /** Once, at startup: a file for everything from INFO up, the console only for warnings. */
    public static void configure() {
        Logger root = Logger.getLogger("");
        for (var h : root.getHandlers()) root.removeHandler(h);
        root.setLevel(Level.INFO);
        Formatter line = new Formatter() {
            @Override
            public String format(LogRecord r) {
                StringBuilder s = new StringBuilder()
                        .append(TIME.format(LocalDateTime.ofInstant(r.getInstant(), ZoneId.systemDefault())))
                        .append(' ').append(r.getLevel().getName())
                        .append(' ').append(shorten(r.getLoggerName()))
                        // Line breaks in messages (server errors, host names) must not create fake lines in the log.
                        .append(": ").append(formatMessage(r).replace("\r", "\\r").replace("\n", "\\n")).append('\n');
                if (r.getThrown() != null) {
                    var w = new StringWriter();
                    r.getThrown().printStackTrace(new PrintWriter(w));
                    s.append(w);
                }
                return s.toString();
            }
        };
        ConsoleHandler console = new ConsoleHandler();
        console.setLevel(Level.WARNING);
        console.setFormatter(line);
        root.addHandler(console);
        try {
            Files.createDirectories(DIR);
            try {
                // The log tells which servers are used: readable by the user only.
                Files.setPosixFilePermissions(DIR, PosixFilePermissions.fromString("rwx------"));
            } catch (UnsupportedOperationException e) {
                // File system without POSIX permissions.
            }
            FileHandler file = new FileHandler(DIR.resolve("lemuri-%g.log").toString(), 1_000_000, 3, true);
            file.setEncoding("UTF-8");
            file.setFormatter(line);
            root.addHandler(file);
        } catch (IOException e) {
            of(Log.class).warning("Cannot write the log in " + DIR + ": " + e.getMessage());
        }
        // An exception nobody handles ends up here instead of getting lost on the console.
        Thread.setDefaultUncaughtExceptionHandler((t, e) -> of(Log.class).log(Level.SEVERE, "Uncaught exception in thread " + t.getName(), e));
    }

    /** "lemuri.be.sharing.Lan" → "Lan"; library names stay whole. */
    private static String shorten(String name) {
        if (name == null) return "";
        return name.startsWith("lemuri.") ? name.substring(name.lastIndexOf('.') + 1) : name;
    }
}
