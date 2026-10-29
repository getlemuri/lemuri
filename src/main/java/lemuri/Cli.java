package lemuri;

import java.io.BufferedReader;
import java.io.File;
import java.io.FileInputStream;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.Base64;
import java.util.Comparator;
import java.util.List;
import java.util.Locale;
import lemuri.be.dto.Host;
import lemuri.be.storage.FileVault;
import lemuri.be.storage.JsonHostRepository;
import lemuri.be.terminal.Jumps;
import lemuri.be.terminal.SshCli;
import lemuri.be.util.Folders;
import lemuri.be.util.I18n;
import lemuri.be.util.PrivateFiles;

/**
 * The command line, without the window: {@code lemuri -s HOST} gives the command that opens an SSH shell to
 * that host in the terminal, {@code lemuri -l} lists the hosts. The command is written to standard output
 * with its words separated by NUL, for the launcher script to run it with exec ({@code mapfile -d ''}):
 * Java cannot hand its terminal over to ssh. Words starting with {@value #ENV} are environment variables
 * for ssh, set by the script with export (never as arguments: ps shows those to every user).
 * <p>Saved passwords (the host's and its jump hosts') come from the vault, unlocked with its password typed
 * in the terminal; ssh gets them through SSH_ASKPASS, answered by the script (see ~/.local/bin/lemuri).
 */
public final class Cli {
    static final String ENV = "env:";
    /** Variable with the saved passwords for the askpass of the script: "user@address TAB base64(password)" per line, all in base64. */
    static final String PASSWORDS = "LEMURI_PASSWORDS";
    private static final int TRIES = 3;

    private Cli() {}

    /** The first argument the command line answers to: anything else starts the app. */
    public static boolean handles(String[] args) {
        return args.length > 0 && List.of("-s", "--shell", "-l", "--list", "-h", "--help").contains(args[0]);
    }

    public static void main(String[] args) {
        System.exit(run(args, System.out, System.err));
    }

    static int run(String[] args, PrintStream out, PrintStream err) {
        if (!handles(args) || args[0].equals("-h") || args[0].equals("--help")) {
            err.println(I18n.t("cli.usage"));
            return handles(args) ? 0 : 2;
        }
        List<Host> hosts;
        try {
            hosts = JsonHostRepository.peek();
        } catch (IOException e) {
            err.println(I18n.t("cli.err.hosts", e.getMessage()));
            return 1;
        }
        if (args[0].equals("-l") || args[0].equals("--list")) {
            hosts.stream().sorted(Comparator.comparing((Host h) -> Folders.label(h.group)).thenComparing(h -> h.name, String.CASE_INSENSITIVE_ORDER))
                    .forEach(h -> out.println(h.name + "\t" + h.user + "@" + h.address + (h.port != 22 ? ":" + h.port : "") + "\t" + Folders.label(h.group)));
            return 0;
        }
        if (args.length < 2) {
            err.println(I18n.t("cli.usage"));
            return 2;
        }
        String name = args[1];
        List<Host> found = hosts.stream().filter(h -> name.equalsIgnoreCase(h.name)).toList();
        if (found.isEmpty()) {
            err.println(I18n.t("cli.err.notFound", name));
            hosts.stream().filter(h -> h.name.toLowerCase(Locale.ROOT).contains(name.toLowerCase(Locale.ROOT)))
                    .forEach(h -> err.println("  " + h.name));
            return 1;
        }
        if (found.size() > 1) {
            err.println(I18n.t("cli.err.many", name));
            found.forEach(h -> err.println("  " + h.name + "  (" + Folders.label(h.group) + ")"));
            return 1;
        }
        Host h = Jumps.resolve(found.get(0), hosts);
        try {
            String config = SshCli.config(h);
            // One file per host: two terminals opening two hosts at once do not overwrite each other's.
            Path file = PrivateFiles.DIR.resolve("ssh").resolve(safe(h.id) + ".conf");
            PrivateFiles.write(file, config);
            List<String> command = new ArrayList<>();
            String table = passwords(h, err);
            if (table == null) return 1;
            if (!table.isEmpty()) command.add(ENV + PASSWORDS + "=" + Base64.getEncoder().encodeToString(table.getBytes(StandardCharsets.UTF_8)));
            command.addAll(SshCli.command(file.toString(), Arrays.asList(args).subList(2, args.length)));
            out.write(String.join("\0", command).concat("\0").getBytes(StandardCharsets.UTF_8));
            out.flush();
            return 0;
        } catch (IllegalArgumentException | IOException e) {
            err.println(e.getMessage());
            return 1;
        }
    }

    /**
     * The saved passwords of the host and its jump hosts that log in with a password, from the vault opened
     * with its password typed in the terminal. Empty if none is needed or none is saved (ssh asks, as usual);
     * null if the user gave up on the vault password.
     */
    private static String passwords(Host h, PrintStream err) throws IOException {
        List<Host> all = new ArrayList<>(h.route != null ? h.route : List.of());
        all.add(h);
        List<Host> withPassword = all.stream().filter(x -> x.authMethod == Host.AuthMethod.PASSWORD).toList();
        if (withPassword.isEmpty()) return "";
        FileVault vault = new FileVault();
        if (!vault.exists()) {
            err.println(I18n.t("cli.noVault"));
            return "";
        }
        for (int i = 0; i < TRIES; i++) {
            char[] main = Terminal.readSecret(I18n.t("cli.vaultPassword"));
            if (main == null) return null;
            try {
                if (vault.open(main)) break;
            } finally {
                Arrays.fill(main, '\0');
            }
            err.println(I18n.t("cli.wrongPassword"));
            if (i == TRIES - 1) return null;
        }
        StringBuilder table = new StringBuilder();
        for (Host x : withPassword) {
            String password = vault.password(x.id);
            if (password == null) continue;
            table.append(x.user).append('@').append(x.address).append('\t')
                    .append(Base64.getEncoder().encodeToString(password.getBytes(StandardCharsets.UTF_8))).append('\n');
        }
        vault.lock();
        return table.toString();
    }

    /** Reads from the terminal itself (standard output is the script's pipe), without echo. */
    static final class Terminal {
        private Terminal() {}

        static char[] readSecret(String prompt) {
            File tty = new File("/dev/tty");
            try (FileOutputStream to = new FileOutputStream(tty)) {
                to.write(prompt.getBytes(StandardCharsets.UTF_8));
                to.flush();
                stty(tty, "-echo");
                try (BufferedReader in = new BufferedReader(new InputStreamReader(new FileInputStream(tty), StandardCharsets.UTF_8))) {
                    String line = in.readLine();
                    return line == null ? null : line.toCharArray();
                } finally {
                    stty(tty, "echo");
                    to.write('\n');
                }
            } catch (IOException e) {
                return null;
            }
        }

        private static void stty(File tty, String mode) throws IOException {
            try {
                new ProcessBuilder("stty", mode).redirectInput(tty).start().waitFor();
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
            }
        }
    }

    /** The host id as a file name: only letters, digits, dash and underscore. */
    private static String safe(String id) {
        String s = id == null ? "" : id.replaceAll("[^A-Za-z0-9_-]", "_");
        return s.isEmpty() ? "host" : s;
    }
}
