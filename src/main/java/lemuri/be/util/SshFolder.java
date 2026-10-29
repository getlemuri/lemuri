package lemuri.be.util;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.LinkOption;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.nio.file.attribute.PosixFilePermissions;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Set;
import java.util.regex.Pattern;

/**
 * The files of ~/.ssh that a backup can carry: private keys (with their .pub), known_hosts and config, and
 * nothing else (see {@link #kind}). Only plain file names, never a path; nothing existing is overwritten
 * when they are put back, and known_hosts never gets @cert-authority or @revoked lines from a backup.
 */
public final class SshFolder {
    public static final String KNOWN_HOSTS = "known_hosts", CONFIG = "config";
    /** A key or a config file is a few KB. */
    private static final long MAX_SIZE = 256 * 1024;
    private static final Pattern NAME = Pattern.compile("[A-Za-z0-9_@+-][A-Za-z0-9._@+-]{0,99}");

    /**
     * Files that sshd or ssh read with effects far beyond a key: whoever wrote a backup must never be able to
     * plant them (a login to this PC, commands at every login, another known hosts file).
     */
    private static final Pattern FORBIDDEN = Pattern.compile("authorized_keys.*|rc|environment|known_hosts2.*");
    /** config directives that run commands on this PC when ssh connects. */
    private static final Pattern RUNS_COMMANDS = Pattern.compile("(?im)^\\s*(ProxyCommand|LocalCommand|PermitLocalCommand|KnownHostsCommand|Match\\s.*\\bexec)\\b.*$");

    private SshFolder() {}

    /** What a file of ~/.ssh can be in a backup. */
    public enum Kind { PRIVATE_KEY, PUBLIC_KEY, KNOWN_HOSTS, CONFIG }

    /**
     * What this file is, from its name and its content; null if a backup may not carry it: a name that is not
     * a bare one, the files of {@link #FORBIDDEN}, a ".pub" that is not one public key line, or anything else
     * that is not a private key.
     */
    public static Kind kind(String name, byte[] content) {
        if (!safeName(name) || FORBIDDEN.matcher(name).matches()) return null;
        if (name.equals(KNOWN_HOSTS)) return Kind.KNOWN_HOSTS;
        if (name.equals(CONFIG)) return Kind.CONFIG;
        String s = new String(content, StandardCharsets.UTF_8);
        if (name.endsWith(".pub")) return s.strip().lines().count() == 1 && s.strip().matches("(ssh-|ecdsa-|sk-)\\S+ [A-Za-z0-9+/=]+( .*)?") ? Kind.PUBLIC_KEY : null;
        return looksPrivate(s) ? Kind.PRIVATE_KEY : null;
    }

    private static boolean looksPrivate(String head) {
        return head.startsWith("-----BEGIN") && head.lines().findFirst().orElse("").contains("PRIVATE KEY") || head.startsWith("PuTTY-User-Key-File");
    }

    /** The config directives in {@code content} that run commands, such as "ProxyCommand": shown before importing it. */
    public static List<String> commandDirectives(byte[] content) {
        List<String> l = new ArrayList<>();
        var m = RUNS_COMMANDS.matcher(new String(content, StandardCharsets.UTF_8));
        while (m.find()) {
            String d = m.group(1).regionMatches(true, 0, "Match", 0, 5) ? "Match exec" : m.group(1);
            if (!l.contains(d)) l.add(d);
        }
        return l;
    }

    /** known_hosts lines a backup may never add: a CA for any host, or a key marked as revoked. */
    static boolean marker(String line) {
        String l = line.strip();
        return l.startsWith("@cert-authority") || l.startsWith("@revoked");
    }

    /** The known_hosts lines an import would add here: the missing ones, without @cert-authority or @revoked. */
    public static List<String> newKnownHosts(byte[] arrived) throws IOException {
        Path p = dir().resolve(KNOWN_HOSTS);
        byte[] here = Files.isRegularFile(p, LinkOption.NOFOLLOW_LINKS) ? Files.readAllBytes(p) : new byte[0];
        return new String(missingLines(here, arrived), StandardCharsets.UTF_8).lines().filter(l -> !l.isBlank()).toList();
    }

    /** How many @cert-authority or @revoked lines a known_hosts from a backup has: they are left out. */
    public static long markerLines(byte[] arrived) {
        return new String(arrived, StandardCharsets.UTF_8).lines().filter(SshFolder::marker).count();
    }

    public static Path dir() {
        return Path.of(System.getProperty("user.home"), ".ssh");
    }

    /** A bare file name, safe to join to ~/.ssh: no folders, no "..", not hidden. */
    public static boolean safeName(String name) {
        return name != null && NAME.matcher(name).matches();
    }

    /** The private keys in ~/.ssh, recognised by their content (not by the name), sorted by name. */
    public static List<String> privateKeys() {
        List<String> l = new ArrayList<>();
        try (var files = Files.list(dir())) {
            for (Path p : files.toList()) {
                Path f = p.getFileName();
                if (f == null) continue;
                String n = f.toString();
                if (safeName(n) && !n.endsWith(".pub") && isPrivateKey(p)) l.add(n);
            }
        } catch (IOException e) {
            // no ~/.ssh: no keys
        }
        l.sort(String.CASE_INSENSITIVE_ORDER);
        return l;
    }

    /** True if ~/.ssh has this file (a regular one, not a link). */
    public static boolean exists(String name) {
        return safeName(name) && Files.isRegularFile(dir().resolve(name), LinkOption.NOFOLLOW_LINKS);
    }

    /** The content of a file of ~/.ssh: only regular files, not links, and not too big. */
    public static byte[] read(String name) throws IOException {
        if (!safeName(name)) throw new IOException(I18n.t("backup.badSshName", name));
        Path p = dir().resolve(name);
        if (!Files.isRegularFile(p, LinkOption.NOFOLLOW_LINKS) || Files.size(p) > MAX_SIZE) throw new IOException(I18n.t("backup.sshNotReadable", name));
        return Files.readAllBytes(p);
    }

    private static boolean isPrivateKey(Path p) {
        try {
            if (!Files.isRegularFile(p, LinkOption.NOFOLLOW_LINKS) || Files.size(p) > MAX_SIZE) return false;
            byte[] head = new byte[64];
            int n;
            try (var in = Files.newInputStream(p, LinkOption.NOFOLLOW_LINKS)) {
                n = in.readNBytes(head, 0, head.length);
            }
            return looksPrivate(new String(head, 0, n, StandardCharsets.US_ASCII));
        } catch (IOException e) {
            return false;
        }
    }

    /** What happened to a file put back in ~/.ssh. */
    public enum Outcome { ADDED, SAME, BESIDE, MERGED }

    /** {@code savedAs}: the name it was written with (another one for {@link Outcome#BESIDE}). */
    public record Installed(String name, String savedAs, Outcome outcome) {}

    /**
     * Puts a file back in ~/.ssh. A missing file is written (readable by the user only, a .pub by everyone);
     * the same content is left alone; known_hosts gets the lines it lacks; any other existing file stays as
     * it is and the one that arrived is written beside it as {@code name.imported}.
     */
    public static Installed install(String name, byte[] content) throws IOException {
        if (!safeName(name)) throw new IOException(I18n.t("backup.badSshName", name));
        // Checked again here, not only when the backup is opened: this is where the file is written.
        if (kind(name, content) == null) throw new IOException(I18n.t("backup.sshNotAllowed", name));
        Path d = dir();
        if (!Files.isDirectory(d)) {
            Files.createDirectories(d);
            try {
                Files.setPosixFilePermissions(d, PosixFilePermissions.fromString("rwx------"));
            } catch (UnsupportedOperationException e) {
                // no POSIX permissions here
            }
        }
        Path p = d.resolve(name);
        if (!Files.exists(p, LinkOption.NOFOLLOW_LINKS)) {
            // A new known_hosts too gets only plain host keys, never @cert-authority or @revoked lines.
            write(p, name.equals(KNOWN_HOSTS) ? missingLines(new byte[0], content) : content, name.endsWith(".pub"));
            return new Installed(name, name, Outcome.ADDED);
        }
        if (Files.isRegularFile(p, LinkOption.NOFOLLOW_LINKS)) {
            byte[] here = Files.readAllBytes(p);
            if (Arrays.equals(here, content)) return new Installed(name, name, Outcome.SAME);
            if (name.equals(KNOWN_HOSTS)) {
                byte[] missing = missingLines(here, content);
                if (missing.length == 0) return new Installed(name, name, Outcome.SAME);
                Files.write(p, missing, StandardOpenOption.APPEND, LinkOption.NOFOLLOW_LINKS);
                return new Installed(name, name, Outcome.MERGED);
            }
        }
        for (int i = 1; ; i++) {
            String beside = name + ".imported" + (i > 1 ? "-" + i : "");
            Path b = d.resolve(beside);
            if (Files.exists(b, LinkOption.NOFOLLOW_LINKS)) continue;
            write(b, content, name.endsWith(".pub"));
            return new Installed(name, beside, Outcome.BESIDE);
        }
    }

    /** What {@link #install} would do with this file, without writing anything: to show it before asking. */
    public static Outcome preview(String name, byte[] content) {
        Path p = dir().resolve(name);
        if (!safeName(name) || !Files.exists(p, LinkOption.NOFOLLOW_LINKS)) return Outcome.ADDED;
        if (!Files.isRegularFile(p, LinkOption.NOFOLLOW_LINKS)) return Outcome.BESIDE;
        try {
            byte[] here = Files.readAllBytes(p);
            if (Arrays.equals(here, content)) return Outcome.SAME;
            if (name.equals(KNOWN_HOSTS)) return missingLines(here, content).length == 0 ? Outcome.SAME : Outcome.MERGED;
            return Outcome.BESIDE;
        } catch (IOException e) {
            return Outcome.BESIDE;
        }
    }

    /** The lines of {@code arrived} that {@code here} does not have, ready to be appended. */
    static byte[] missingLines(byte[] here, byte[] arrived) {
        String h = new String(here, StandardCharsets.UTF_8);
        Set<String> have = new LinkedHashSet<>(h.lines().toList());
        StringBuilder b = new StringBuilder();
        if (!h.isEmpty() && !h.endsWith("\n")) b.append('\n');
        int start = b.length();
        for (String l : new String(arrived, StandardCharsets.UTF_8).lines().toList())
            if (!l.isBlank() && !marker(l) && have.add(l)) b.append(l).append('\n');
        return b.length() == start ? new byte[0] : b.toString().getBytes(StandardCharsets.UTF_8);
    }

    /** A new file, born with its permissions: never an existing one, never through a link. */
    private static void write(Path p, byte[] content, boolean everyone) throws IOException {
        try {
            Files.createFile(p, PosixFilePermissions.asFileAttribute(PosixFilePermissions.fromString(everyone ? "rw-r--r--" : "rw-------")));
        } catch (UnsupportedOperationException e) {
            Files.createFile(p);
        }
        Files.write(p, content, StandardOpenOption.WRITE, StandardOpenOption.TRUNCATE_EXISTING, LinkOption.NOFOLLOW_LINKS);
    }
}
