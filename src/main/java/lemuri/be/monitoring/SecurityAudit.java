package lemuri.be.monitoring;

import java.math.BigInteger;
import java.nio.ByteBuffer;
import java.nio.charset.StandardCharsets;
import java.nio.file.FileSystems;
import java.nio.file.Path;
import java.nio.file.PathMatcher;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import lemuri.be.dto.Host;
import lemuri.be.dto.Security;
import lemuri.be.dto.Security.Check;
import lemuri.be.dto.Security.Level;

/**
 * The SSH security checks of a host, worked out from what the connection and the server tell. Pure: no network,
 * no files; {@link Metrics} gathers the facts. Only judges, never changes anything.
 */
public final class SecurityAudit {
    private SecurityAudit() {}

    /**
     * What the server is asked, read only and without sudo: the user id, the sshd settings (all of them from
     * {@code sshd -T} when logged in as root, otherwise the configuration files, which anyone can read) and the
     * brute force protections running. The brackets in the process names keep the command itself from matching.
     */
    static final String COMMAND = String.join("\n",
            "echo \"I $(id -u)\"",
            "T=$(command -v timeout >/dev/null 2>&1 && echo 'timeout 10')",
            "D=$(command -v sshd 2>/dev/null || echo /usr/sbin/sshd)",
            "if [ \"$(id -u)\" = 0 ] && [ -x \"$D\" ]; then $T \"$D\" -T 2>/dev/null | sed 's/^/S /'; fi",
            "for f in /etc/ssh/sshd_config /etc/ssh/sshd_config.d/*.conf; do [ -r \"$f\" ] && { echo \"F $f\"; sed 's/^/L /' \"$f\"; }; done",
            "(systemctl is-active --quiet fail2ban 2>/dev/null || pgrep -f '[f]ail2ban-server' >/dev/null 2>&1) && echo 'G fail2ban'",
            "(systemctl is-active --quiet crowdsec 2>/dev/null || pgrep -x '[c]rowdsec' >/dev/null 2>&1) && echo 'G crowdsec'",
            "(systemctl is-active --quiet sshguard 2>/dev/null || pgrep -x '[s]shguard' >/dev/null 2>&1) && echo 'G sshguard'",
            "true");

    /** What the SSH connection itself shows: the negotiated algorithms and the server's key and version. */
    public record Connection(String serverVersion, String kex, String cipher, String mac, boolean strictKex,
                             String hostKeyAlgorithm, int hostKeyBits) {}

    /** The private key file used to log in: type (as ssh-keygen calls it), bits, protected by a passphrase. */
    public record ClientKey(String type, int bits, boolean encrypted) {}

    /**
     * What the server told: user id (null if unknown), the sshd settings by lower case name (null if they could
     * not be read), the brute force protections found running.
     */
    public record Server(Integer uid, Map<String, String> sshd, List<String> guards) {
        public Server {
            sshd = sshd == null ? null : Map.copyOf(sshd);
            guards = List.copyOf(guards);
        }
    }

    // ── Server answer ──

    static Server parse(String text) {
        Integer uid = null;
        Map<String, String> effective = new LinkedHashMap<>();
        Map<String, List<String>> files = new LinkedHashMap<>();
        List<String> guards = new ArrayList<>();
        List<String> current = null;
        for (String line : text.split("\n", -1)) {
            if (line.startsWith("I ")) {
                try {
                    uid = Integer.parseInt(line.substring(2).trim());
                } catch (NumberFormatException e) {
                    // A welcome message in the way: unknown.
                }
            } else if (line.startsWith("S ")) {
                String[] p = line.substring(2).trim().split("\\s+", 2);
                // sshd -T writes every setting once, in lower case; some (port, listenaddress) more than once: the first.
                if (p.length == 2) effective.putIfAbsent(p[0].toLowerCase(Locale.ROOT), p[1].trim());
            } else if (line.startsWith("F ")) {
                current = new ArrayList<>();
                files.put(line.substring(2).trim(), current);
            } else if (line.startsWith("L ") || line.equals("L")) {
                if (current != null) current.add(line.length() > 2 ? line.substring(2) : "");
            } else if (line.startsWith("G ")) {
                guards.add(line.substring(2).trim());
            }
        }
        Map<String, String> sshd = !effective.isEmpty() ? effective : files.isEmpty() ? null : sshdConfig(files);
        return new Server(uid, sshd, guards);
    }

    /**
     * The settings of sshd_config as sshd reads them: the first value of each wins, Include brings in the files
     * in their place (in name order), Match blocks are skipped (they apply to some logins only, up to the next
     * Match or the end of their file). Only the files given are known: an Include of others finds nothing.
     */
    static Map<String, String> sshdConfig(Map<String, List<String>> files) {
        Map<String, String> out = new HashMap<>();
        String main = files.containsKey("/etc/ssh/sshd_config") ? "/etc/ssh/sshd_config" : files.keySet().iterator().next();
        read(main, files, out, 0);
        return out;
    }

    private static void read(String file, Map<String, List<String>> files, Map<String, String> out, int depth) {
        if (depth > 8) return;
        boolean inMatch = false;
        for (String raw : files.getOrDefault(file, List.of())) {
            String line = raw.strip();
            if (line.isEmpty() || line.startsWith("#")) continue;
            String[] p = line.split("[\\s=]+", 2);
            String key = p[0].toLowerCase(Locale.ROOT);
            String value = p.length > 1 ? unquote(p[1].strip()) : "";
            if (key.equals("match")) {
                inMatch = !value.equalsIgnoreCase("all");
                continue;
            }
            if (inMatch) continue;
            if (key.equals("include")) {
                for (String pattern : value.split("\\s+")) {
                    String full = pattern.startsWith("/") ? pattern : "/etc/ssh/" + pattern;
                    PathMatcher m = FileSystems.getDefault().getPathMatcher("glob:" + full);
                    files.keySet().stream().filter(f -> m.matches(Path.of(f))).sorted()
                            .forEach(f -> read(f, files, out, depth + 1));
                }
                continue;
            }
            // An old name, as sshd reads it.
            if (key.equals("challengeresponseauthentication")) key = "kbdinteractiveauthentication";
            out.putIfAbsent(key, value);
        }
    }

    private static String unquote(String v) {
        return v.length() >= 2 && v.startsWith("\"") && v.endsWith("\"") ? v.substring(1, v.length() - 1) : v;
    }

    // ── Checks ──

    /**
     * All the checks that can be judged: those of the connection always, those of the key only with a key file,
     * those of the server when it answered. {@code serverError} goes in the report when the server could not be read.
     */
    public static Security audit(Host.AuthMethod auth, ClientKey key, Connection c, Server s, String serverError, long when) {
        List<Check> l = new ArrayList<>();
        if (c != null) connection(c, l);
        l.add(auth == Host.AuthMethod.PASSWORD
                ? new Check("client.auth", Level.BAD, "password", "ssh-copy-id", 3)
                : new Check("client.auth", Level.OK, auth == Host.AuthMethod.AGENT ? "ssh-agent" : "key", null, 3));
        if (key != null) {
            String t = key.type().toLowerCase(Locale.ROOT);
            Level lv = t.contains("ed25519") || t.contains("ecdsa") ? Level.OK
                    : t.contains("rsa") ? (key.bits() >= 3072 ? Level.OK : key.bits() >= 2048 ? Level.WARN : Level.BAD)
                    : Level.BAD;
            l.add(new Check("client.key", lv, key.type() + (t.contains("rsa") ? " " + key.bits() + " bit" : ""),
                    lv == Level.OK ? null : "ssh-keygen -t ed25519", 2));
            l.add(key.encrypted() ? new Check("client.passphrase", Level.OK, null, null, 1)
                    : new Check("client.passphrase", Level.WARN, null, "ssh-keygen -p", 1));
        }
        if (s != null) server(s, l);
        return new Security(l, serverError, when);
    }

    private static void connection(Connection c, List<Check> l) {
        String v = c.serverVersion() == null ? "" : c.serverVersion();
        Matcher m = Pattern.compile("OpenSSH_(\\d+)\\.(\\d+)").matcher(v);
        String shown = v.startsWith("SSH-2.0-") ? v.substring(8) : v;
        if (m.find()) {
            int major = Integer.parseInt(m.group(1)), minor = Integer.parseInt(m.group(2));
            // Before 7.4 (2016) it no longer gets fixes anywhere; newer ones are kept fixed by the distributions
            // without changing the number, so the number alone says nothing more.
            boolean old = major < 7 || major == 7 && minor < 4;
            l.add(new Check("conn.version", old ? Level.BAD : Level.OK, shown, null, 2));
        } else if (!shown.isEmpty()) {
            l.add(new Check("conn.version", Level.OK, shown, null, 2));
        }

        String kex = lower(c.kex());
        l.add(new Check("conn.kex", kex.contains("sha1") || kex.contains("group1-") ? Level.BAD : Level.OK, c.kex(), null, 2));

        String cipher = lower(c.cipher());
        boolean weakCipher = cipher.contains("cbc") || cipher.contains("3des") || cipher.contains("arcfour") || cipher.contains("blowfish");
        l.add(new Check("conn.cipher", weakCipher ? Level.BAD : Level.OK, c.cipher(), null, 2));

        // With an AEAD cipher the MAC is part of the cipher: the separate one is not used.
        boolean aead = cipher.contains("gcm") || cipher.contains("chacha20-poly1305");
        String mac = lower(c.mac());
        Level macLevel = aead ? Level.OK : mac.contains("md5") || mac.endsWith("-96") ? Level.BAD : mac.contains("sha1") ? Level.WARN : Level.OK;
        l.add(new Check("conn.mac", macLevel, aead ? c.cipher() : c.mac(), null, 1));

        // Terrapin (CVE-2023-48795) hits ChaCha20-Poly1305 and CBC with Encrypt-then-MAC, unless both sides use strict kex.
        boolean exposed = cipher.contains("chacha20-poly1305") || cipher.contains("cbc") && mac.contains("-etm");
        l.add(new Check("conn.terrapin", c.strictKex() || !exposed ? Level.OK : Level.BAD,
                c.strictKex() ? "strict kex" : null, null, 2));

        String hk = lower(c.hostKeyAlgorithm());
        Level hkLevel;
        if (hk.equals("ssh-rsa") || hk.contains("dss")) hkLevel = Level.BAD;
        else if (hk.contains("rsa")) hkLevel = c.hostKeyBits() >= 3072 || c.hostKeyBits() == 0 ? Level.OK : c.hostKeyBits() >= 2048 ? Level.WARN : Level.BAD;
        else hkLevel = Level.OK;
        l.add(new Check("conn.hostkey", hkLevel,
                c.hostKeyAlgorithm() + (hk.contains("rsa") && c.hostKeyBits() > 0 ? " " + c.hostKeyBits() + " bit" : ""), null, 2));
    }

    private static void server(Server s, List<Check> l) {
        Map<String, String> d = s.sshd();
        boolean passwords = false;
        if (d != null) {
            // Defaults of OpenSSH when a setting is not written.
            passwords = yes(d.getOrDefault("passwordauthentication", "yes"));
            boolean kbd = yes(d.getOrDefault("kbdinteractiveauthentication", "yes")) && yes(d.getOrDefault("usepam", "no"));
            l.add(passwords ? new Check("server.password", Level.BAD, "PasswordAuthentication yes", "PasswordAuthentication no", 3)
                    : kbd ? new Check("server.password", Level.WARN, "KbdInteractiveAuthentication yes", "KbdInteractiveAuthentication no", 3)
                    : new Check("server.password", Level.OK, "PasswordAuthentication no", null, 3));

            String root = d.getOrDefault("permitrootlogin", "prohibit-password").toLowerCase(Locale.ROOT);
            Level rootLevel = root.equals("no") ? Level.OK : root.equals("yes") ? Level.BAD : Level.WARN;
            l.add(new Check("server.root", rootLevel, "PermitRootLogin " + root, rootLevel == Level.OK ? null : "PermitRootLogin no", 2));

            String port = d.getOrDefault("port", "22").split("\\s+")[0];
            l.add(new Check("server.port", port.equals("22") ? Level.WARN : Level.OK, "Port " + port, null, 1));
        }
        Set<String> known = Set.of("fail2ban", "crowdsec", "sshguard");
        List<String> on = s.guards().stream().filter(known::contains).toList();
        // Without passwords a brute force attack cannot succeed: the protection only keeps the logs quieter.
        l.add(on.isEmpty()
                ? new Check("server.bruteforce", passwords ? Level.BAD : Level.WARN, null, null, 1)
                : new Check("server.bruteforce", Level.OK, String.join(", ", on), null, 1));
        if (s.uid() != null) l.add(new Check("server.user", s.uid() == 0 ? Level.WARN : Level.OK, s.uid() == 0 ? "root" : "uid " + s.uid(), null, 1));
    }

    private static boolean yes(String v) {
        return v.trim().toLowerCase(Locale.ROOT).startsWith("yes");
    }

    private static String lower(String s) {
        return s == null ? "" : s.toLowerCase(Locale.ROOT);
    }

    /** Bits of the modulus of an ssh-rsa public key blob (as in known_hosts, decoded); 0 if not one. */
    static int rsaBits(byte[] blob) {
        try {
            ByteBuffer b = ByteBuffer.wrap(blob);
            String type = new String(field(b), StandardCharsets.US_ASCII);
            if (!type.equals("ssh-rsa")) return 0;
            field(b); // public exponent
            return new BigInteger(1, field(b)).bitLength();
        } catch (RuntimeException e) {
            return 0;
        }
    }

    private static byte[] field(ByteBuffer b) {
        int n = b.getInt();
        if (n < 0 || n > b.remaining()) throw new IllegalArgumentException("bad length");
        byte[] out = new byte[n];
        b.get(out);
        return out;
    }
}
