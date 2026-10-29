package lemuri.be.terminal;

import com.jcraft.jsch.ChannelSftp;
import com.jcraft.jsch.SftpATTRS;
import com.jcraft.jsch.SftpException;
import com.jcraft.jsch.SftpProgressMonitor;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.InterruptedIOException;
import java.io.OutputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardCopyOption;
import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import lemuri.be.util.I18n;
import lemuri.be.util.RemoteNames;

/**
 * Uploads and downloads over an SFTP channel, without any interface: the queue, the progress bar and
 * the questions are the caller's. Every server path goes through {@link RemoteNames#literal} ("*" and
 * "?" are plain characters), and the local side uses streams, which JSch does not read as patterns.
 */
public final class SftpFiles {
    private SftpFiles() {}

    /** What a running transfer reports to whoever started it, and whether it has been cancelled. */
    public interface Watch extends SftpProgressMonitor {
        boolean cancelled();

        /** An item left out: a symbolic link or a name from the server that is not safe here. */
        void skipped();

        /** The file being transferred now. */
        void file(String name);
    }

    /** The names among {@code files} that already exist in the server folder: an upload would replace them. */
    public static List<String> existing(ChannelSftp c, String folder, List<File> files) {
        List<String> l = new ArrayList<>();
        // Many files: one listing of the folder instead of a round trip to the server for each of them.
        if (files.size() > 3) {
            try {
                Set<String> there = new HashSet<>();
                for (Object o : c.ls(RemoteNames.literal(folder))) there.add(((ChannelSftp.LsEntry) o).getFilename());
                for (File f : files) if (there.contains(f.getName())) l.add(f.getName());
                return l;
            } catch (SftpException e) {
                // the folder cannot be listed: ask file by file
            }
        }
        for (File f : files) {
            try {
                c.lstat(RemoteNames.literal(join(folder, f.getName())));
                l.add(f.getName());
            } catch (SftpException missing) {
                // not there: good
            }
        }
        return l;
    }

    /** The bytes an upload of {@code f} will send: symbolic links are not followed. */
    public static long localSize(File f) {
        if (Files.isSymbolicLink(f.toPath())) return 0;
        if (!f.isDirectory()) return f.length();
        long t = 0;
        File[] children = f.listFiles();
        if (children != null) for (File g : children) t += localSize(g);
        return t;
    }

    /**
     * Uploads a file or a folder into {@code folder}. Symbolic links are not followed: a loop would run
     * forever, and a link would bring to the server files that are outside the chosen folder. Every file
     * goes first to a temporary name and takes the real one only when complete; if anything fails, the
     * temporary file is removed and the one that was there stays.
     */
    public static void upload(ChannelSftp c, File f, String folder, Watch w) throws SftpException, IOException {
        String dest = join(folder, f.getName());
        if (Files.isSymbolicLink(f.toPath())) {
            w.skipped();
            return;
        }
        if (f.isDirectory()) {
            try {
                c.mkdir(dest);
            } catch (SftpException exists) {
                if (!c.stat(RemoteNames.literal(dest)).isDir()) throw new IOException(I18n.t("transfer.notAFolder", dest));
            }
            File[] children = f.listFiles();
            if (children == null) return;
            for (File g : children) {
                upload(c, g, dest, w);
                if (w.cancelled()) return;
            }
            return;
        }
        w.file(f.getName());
        String partial = join(folder, "." + f.getName() + ".lemuri-part");
        try {
            try (InputStream in = Files.newInputStream(f.toPath())) {
                c.put(in, RemoteNames.literal(partial), w, ChannelSftp.OVERWRITE);
            }
            if (w.cancelled()) throw new InterruptedIOException("cancelled");
            // SFTP v3 does not replace with rename: the old file is removed first, if there is one.
            try {
                if (!c.lstat(RemoteNames.literal(dest)).isDir()) c.rm(RemoteNames.literal(dest));
            } catch (SftpException missing) {
                // it was not there
            }
            c.rename(RemoteNames.literal(partial), RemoteNames.literal(dest));
        } catch (SftpException | IOException e) {
            try {
                c.rm(RemoteNames.literal(partial));
            } catch (SftpException already) {
                // no longer there
            }
            if (w.cancelled()) return;
            throw e;
        }
    }

    /** The bytes a download of {@code path} will bring: symbolic links are not followed. */
    public static long remoteSize(ChannelSftp c, String path) throws SftpException {
        SftpATTRS a = c.lstat(RemoteNames.literal(path));
        if (a.isLink()) return 0;
        if (!a.isDir()) return a.getSize();
        long t = 0;
        for (Object o : c.ls(RemoteNames.literal(path))) {
            ChannelSftp.LsEntry e = (ChannelSftp.LsEntry) o;
            if (!safeLocalName(e.getFilename())) continue;
            t += e.getAttrs().isDir() ? remoteSize(c, join(path, e.getFilename())) : e.getAttrs().isLink() ? 0 : e.getAttrs().getSize();
        }
        return t;
    }

    /**
     * A name from the server that can become a file here: not empty, not "." or "..", without "/" or "\" and
     * without control characters. Another name could write outside the chosen folder.
     */
    public static boolean safeLocalName(String n) {
        return n != null && !n.isEmpty() && !n.equals(".") && !n.equals("..") && n.indexOf('/') < 0 && n.indexOf('\\') < 0
                && n.chars().noneMatch(Character::isISOControl);
    }

    /** The last part of a server path: the name the download takes here. */
    public static String lastName(String path) {
        String p = path.endsWith("/") && path.length() > 1 ? path.substring(0, path.length() - 1) : path;
        return p.substring(p.lastIndexOf('/') + 1);
    }

    /**
     * Downloads a file or a folder from the server into the local {@code folder}, as {@link #upload} does the
     * other way: symbolic links on the server are not followed, names that are not safe here are skipped, and
     * every file is written under a temporary name and takes the real one only when complete.
     */
    public static void download(ChannelSftp c, String path, File folder, Watch w) throws SftpException, IOException {
        String name = lastName(path);
        if (!safeLocalName(name)) {
            w.skipped();
            return;
        }
        SftpATTRS a = c.lstat(RemoteNames.literal(path));
        if (a.isLink()) {
            w.skipped();
            return;
        }
        Path dest = folder.toPath().resolve(name);
        if (a.isDir()) {
            if (Files.isSymbolicLink(dest) || Files.exists(dest) && !Files.isDirectory(dest)) throw new IOException(I18n.t("transfer.notAFolder", dest));
            Files.createDirectories(dest);
            for (Object o : c.ls(RemoteNames.literal(path))) {
                String child = ((ChannelSftp.LsEntry) o).getFilename();
                if (child.equals(".") || child.equals("..")) continue;
                if (!safeLocalName(child)) {
                    w.skipped();
                    continue;
                }
                download(c, join(path, child), dest.toFile(), w);
                if (w.cancelled()) return;
            }
            return;
        }
        w.file(name);
        Path partial = folder.toPath().resolve("." + name + ".lemuri-part");
        try {
            try (OutputStream out = Files.newOutputStream(partial)) {
                c.get(RemoteNames.literal(path), out, w);
            }
            if (w.cancelled()) throw new InterruptedIOException("cancelled");
            Files.move(partial, dest, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
        } catch (SftpException | IOException e) {
            Files.deleteIfExists(partial);
            if (w.cancelled()) return;
            throw e;
        }
    }

    public static String join(String folder, String name) {
        return folder.endsWith("/") ? folder + name : folder + "/" + name;
    }
}
