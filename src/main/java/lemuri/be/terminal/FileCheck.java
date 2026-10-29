package lemuri.be.terminal;

import com.jcraft.jsch.ChannelSftp;
import com.jcraft.jsch.SftpATTRS;
import com.jcraft.jsch.SftpException;
import java.io.IOException;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import lemuri.be.util.RemoteNames;

/**
 * What a name in the terminal is, before a double click acts on it: a folder is listed, a text file of a
 * reasonable size is opened with nano. A binary file would fill nano with garbage, and a large one (a log of
 * gigabytes) would take nano a long time to load: those are not opened. Links are followed, as cd and nano do.
 */
public final class FileCheck {
    private FileCheck() {}

    public enum Kind { FOLDER, TEXT, BINARY, LARGE, OTHER, MISSING }

    /** Larger than this, a file is not opened with nano. */
    public static final long MAX_TEXT = 1024 * 1024;
    /** How much of the start of a file is read to tell text from binary, as git and grep do. */
    static final int HEAD = 8192;

    /** What {@code path} is on the server, over an SFTP channel of the session. */
    public static Kind remote(ChannelSftp c, String path) {
        SftpATTRS a;
        try {
            a = c.stat(RemoteNames.literal(path));
        } catch (SftpException missing) {
            return Kind.MISSING;
        }
        if (a.isDir()) return Kind.FOLDER;
        if (!a.isReg()) return Kind.OTHER;
        if (a.getSize() > MAX_TEXT) return Kind.LARGE;
        try (InputStream in = c.get(RemoteNames.literal(path))) {
            return judge(in);
        } catch (SftpException | IOException e) {
            // Not readable (no permission): nano would say so better than a guess.
            return Kind.TEXT;
        }
    }

    /** What {@code path} is on this computer, for a local shell. */
    public static Kind local(Path path) {
        if (Files.isDirectory(path)) return Kind.FOLDER;
        if (!Files.exists(path)) return Kind.MISSING;
        if (!Files.isRegularFile(path)) return Kind.OTHER;
        try {
            if (Files.size(path) > MAX_TEXT) return Kind.LARGE;
            try (InputStream in = Files.newInputStream(path)) {
                return judge(in);
            }
        } catch (IOException e) {
            return Kind.TEXT;
        }
    }

    /** Binary if a NUL byte appears in the first {@link #HEAD} bytes: text files never have one. */
    static Kind judge(InputStream in) throws IOException {
        byte[] head = in.readNBytes(HEAD);
        for (byte b : head) if (b == 0) return Kind.BINARY;
        return Kind.TEXT;
    }
}
