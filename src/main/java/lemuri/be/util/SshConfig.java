package lemuri.be.util;

import java.io.IOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileSystems;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.PathMatcher;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.UUID;
import java.util.regex.Pattern;
import java.util.stream.Stream;
import lemuri.be.dto.Host;

/**
 * Reads the hosts of an OpenSSH client configuration (~/.ssh/config), with the same rules as ssh: every
 * concrete name of a {@code Host} line is a host, and for each option the first value found among the
 * blocks that match it wins, so {@code Host *} at the end gives the defaults. {@code Include} is followed;
 * {@code Match} blocks cannot be evaluated here and are skipped. Only what Lemuri uses is read.
 */
public final class SshConfig {
    /** Git hosting services: reachable over SSH but not servers to open a shell on. */
    private static final Set<String> GIT_SERVICES = Set.of("github.com", "gitlab.com", "bitbucket.org", "ssh.dev.azure.com",
            "vs-ssh.visualstudio.com", "codeberg.org", "git.sr.ht");
    private static final int MAX_INCLUDE_DEPTH = 16;

    private SshConfig() {}

    /** A host as configured. {@code user} null: ssh would use the local user. {@code identityFile} already expanded. */
    public record Entry(String alias, String hostName, String user, int port, String identityFile, String proxyJump) {
        /** A git hosting service rather than a server. */
        public boolean gitService() {
            return GIT_SERVICES.contains(hostName.toLowerCase(Locale.ROOT));
        }

        /** "user@host:port", as shown in the import list. */
        public String destination() {
            return (user != null ? user : System.getProperty("user.name")) + "@" + hostName + ":" + port;
        }

        /**
         * The host for Lemuri, in {@code group}. With an IdentityFile it uses that key (a name if it is in ~/.ssh,
         * otherwise the full path); without, ssh-agent and the keys in ~/.ssh, as ssh would.
         */
        public Host toHost(String group) {
            Host h = new Host();
            h.id = UUID.randomUUID().toString().substring(0, 8);
            h.name = alias;
            h.address = hostName;
            h.port = port;
            h.user = user != null ? user : System.getProperty("user.name");
            h.group = group;
            h.jump = proxyJump;
            if (identityFile != null) {
                h.authMethod = Host.AuthMethod.KEY;
                Path k = Path.of(identityFile), ssh = sshDir();
                Path name = k.getFileName();
                h.key = name != null && ssh.equals(k.getParent()) ? name.toString() : identityFile;
            } else {
                h.authMethod = Host.AuthMethod.AGENT;
            }
            return h;
        }
    }

    /** One block of the file: the lines before the first Host apply to every host. */
    private record Block(List<String> patterns, boolean match, Map<String, String> options) {
        boolean applies(String alias) {
            if (match) return false;
            if (patterns == null) return true;
            boolean any = false;
            for (String p : patterns) {
                boolean negated = p.startsWith("!");
                if (glob(negated ? p.substring(1) : p, alias)) {
                    if (negated) return false;
                    any = true;
                }
            }
            return any;
        }
    }

    public static Path sshDir() {
        return Path.of(System.getProperty("user.home"), ".ssh");
    }

    /** The hosts of ~/.ssh/config; empty if the file does not exist. */
    public static List<Entry> read() throws IOException {
        return read(sshDir().resolve("config"));
    }

    public static List<Entry> read(Path file) throws IOException {
        if (!Files.isRegularFile(file)) return List.of();
        List<Block> blocks = new ArrayList<>();
        blocks.add(new Block(null, false, new HashMap<>()));
        Set<String> aliases = new LinkedHashSet<>();
        load(file, blocks, aliases, 0);
        List<Entry> out = new ArrayList<>();
        for (String alias : aliases) {
            Map<String, String> o = new HashMap<>();
            // As ssh does: for each option the first value obtained wins.
            for (Block b : blocks)
                if (b.applies(alias)) b.options().forEach(o::putIfAbsent);
            String hostName = o.getOrDefault("hostname", alias).replace("%h", alias).replace("%%", "%");
            int port;
            try {
                port = Integer.parseInt(o.getOrDefault("port", "22"));
            } catch (NumberFormatException e) {
                port = 22;
            }
            String user = o.get("user");
            String identity = o.get("identityfile");
            if (identity != null) identity = expand(identity, alias, hostName, user);
            String jump = o.get("proxyjump");
            if (jump != null && jump.equalsIgnoreCase("none")) jump = null;
            out.add(new Entry(alias, hostName, user, port, identity, jump));
        }
        return out;
    }

    private static void load(Path file, List<Block> blocks, Set<String> aliases, int depth) throws IOException {
        if (depth > MAX_INCLUDE_DEPTH) return;
        for (String raw : Files.readAllLines(file, StandardCharsets.UTF_8)) {
            String line = raw.strip();
            if (line.isEmpty() || line.startsWith("#")) continue;
            // "Keyword value", "Keyword=value" or "Keyword = value".
            String[] kv = line.split("\\s*=\\s*|\\s+", 2);
            String key = kv[0].toLowerCase(Locale.ROOT), value = kv.length > 1 ? kv[1].strip() : "";
            switch (key) {
                case "host" -> {
                    List<String> patterns = words(value);
                    blocks.add(new Block(patterns, false, new HashMap<>()));
                    for (String p : patterns)
                        if (!p.startsWith("!") && p.chars().noneMatch(c -> c == '*' || c == '?')) aliases.add(p);
                }
                case "match" -> blocks.add(new Block(List.of(), true, new HashMap<>()));
                case "include" -> {
                    for (String pattern : words(value)) for (Path p : include(pattern)) load(p, blocks, aliases, depth + 1);
                }
                default -> {
                    List<String> w = words(value);
                    if (!w.isEmpty()) blocks.get(blocks.size() - 1).options().putIfAbsent(key, w.get(0));
                }
            }
        }
    }

    /** The files of an Include: relative paths are in ~/.ssh, and wildcards are allowed in the file name. */
    private static List<Path> include(String pattern) throws IOException {
        Path p = Path.of(expandHome(pattern));
        if (!p.isAbsolute()) p = sshDir().resolve(p);
        Path dir = p.getParent(), file = p.getFileName();
        if (file == null) return List.of();
        String name = file.toString();
        if (name.chars().noneMatch(c -> c == '*' || c == '?')) return Files.isRegularFile(p) ? List.of(p) : List.of();
        if (dir == null || !Files.isDirectory(dir)) return List.of();
        PathMatcher m = FileSystems.getDefault().getPathMatcher("glob:" + name);
        try (Stream<Path> s = Files.list(dir)) {
            return s.filter(x -> Files.isRegularFile(x) && m.matches(x.getFileName())).sorted().toList();
        }
    }

    /** Space-separated words, where a quoted part is a single word. */
    private static List<String> words(String value) {
        List<String> out = new ArrayList<>();
        StringBuilder w = new StringBuilder();
        boolean quoted = false, any = false;
        for (char c : value.toCharArray()) {
            if (c == '"') {
                quoted = !quoted;
                any = true;
            } else if (Character.isWhitespace(c) && !quoted) {
                if (any) out.add(w.toString());
                w.setLength(0);
                any = false;
            } else {
                w.append(c);
                any = true;
            }
        }
        if (any) out.add(w.toString());
        return out;
    }

    /** ssh patterns: {@code *} any sequence, {@code ?} one character, compared ignoring case. */
    static boolean glob(String pattern, String text) {
        StringBuilder rx = new StringBuilder();
        for (char c : pattern.toCharArray()) {
            if (c == '*') rx.append(".*");
            else if (c == '?') rx.append('.');
            else rx.append(Pattern.quote(String.valueOf(c)));
        }
        return Pattern.compile(rx.toString(), Pattern.CASE_INSENSITIVE).matcher(text).matches();
    }

    private static String expand(String path, String alias, String hostName, String user) {
        String home = System.getProperty("user.home");
        String p = expandHome(path)
                .replace("%d", home)
                .replace("%u", System.getProperty("user.name"))
                .replace("%r", user != null ? user : System.getProperty("user.name"))
                .replace("%h", hostName)
                .replace("%n", alias)
                .replace("%%", "%");
        Path x = Path.of(p);
        return (x.isAbsolute() ? x : sshDir().resolve(x)).normalize().toString();
    }

    private static String expandHome(String p) {
        return p.startsWith("~/") || p.equals("~") ? System.getProperty("user.home") + p.substring(1) : p;
    }
}
