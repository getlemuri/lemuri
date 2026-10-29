package lemuri.be.util;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.nio.file.attribute.PosixFilePermissions;

/**
 * Files readable by the user only (app data in ~/.lemuri): written to a file next to them with
 * permissions 600 and then swapped in one go, so an interruption leaves the previous one.
 */
public final class PrivateFiles {
    private PrivateFiles() {}

    public static final Path DIR = Path.of(System.getProperty("user.home"), ".lemuri");

    public static void write(Path file, String text) throws IOException {
        Path dir = file.toAbsolutePath().getParent();
        if (dir == null) throw new IOException("not a file: " + file);
        folder(dir);
        Path tmp = file.resolveSibling(file.getFileName() + ".tmp");
        Files.writeString(tmp, text, StandardCharsets.UTF_8);
        grants(tmp, "rw-------");
        Files.move(tmp, file, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
    }

    /** Creates the folder if missing and lets only the user open it. */
    public static void folder(Path c) throws IOException {
        Files.createDirectories(c);
        grants(c, "rwx------");
    }

    private static void grants(Path p, String which) throws IOException {
        try {
            Files.setPosixFilePermissions(p, PosixFilePermissions.fromString(which));
        } catch (UnsupportedOperationException e) {
            // File system without POSIX permissions.
        }
    }
}
