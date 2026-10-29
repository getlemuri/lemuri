package lemuri.be.terminal;

import com.jcraft.jsch.AgentIdentityRepository;
import com.jcraft.jsch.AgentProxyException;
import com.jcraft.jsch.ChannelExec;
import com.jcraft.jsch.ChannelSftp;
import com.jcraft.jsch.ChannelShell;
import com.jcraft.jsch.JSch;
import com.jcraft.jsch.JSchException;
import com.jcraft.jsch.SSHAgentConnector;
import com.jcraft.jsch.Session;
import com.jcraft.jsch.UIKeyboardInteractive;
import com.jcraft.jsch.UnixDomainSocketFactory;
import com.jcraft.jsch.UserInfo;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.io.UncheckedIOException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;
import java.util.Locale;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import java.util.function.Function;
import java.util.logging.Logger;
import java.util.regex.Pattern;
import lemuri.be.dto.Host;
import lemuri.be.dto.SessionStatus;
import lemuri.be.service.Transport;
import lemuri.be.util.I18n;
import lemuri.be.util.Log;

/**
 * Real SSH session with JSch. Keys come from ssh-agent or ~/.ssh, host
 * fingerprints from ~/.ssh/known_hosts: a new host must be confirmed by hand
 * (see {@link UserPrompts}). Passwords can come from the vault; key
 * passphrases are asked at every connection and never saved.
 */
public class SshTransport implements Transport {
    private static final Logger LOG = Log.of(SshTransport.class);
    private final Host host;
    private final Function<Host, UserPrompts> requests;
    private Consumer<String> data = d -> {};
    private BiConsumer<SessionStatus, String> state = (s, m) -> {};
    private Consumer<String> os = s -> {};
    private volatile Session session;
    private volatile ChannelShell channel;
    private volatile OutputStream output;
    private volatile boolean isClosed;
    /**
     * The terminal's latest size. It changes while the connection is still being made (the first size can come
     * before the terminal is laid out, a column or two wide): the shell must get the last one, or bash draws its
     * prompt and the typed text for a terminal that narrow, that is, not at all.
     */
    private volatile int columns, lines;

    /** Questions to the user during the connection, answered on the JavaFX thread. */
    public interface UserPrompts {
        boolean confirm(String message);

        String password(String message);

        String passphrase(String message);

        /** A server question that is not the password (2FA code and the like): the answer is not saved. */
        String reply(String question);

        /** Authentication succeeded: a password just typed can now be remembered. */
        default void authenticated() {}
    }

    /** {@code requests}: the questions for each server of the connection, the jump hosts included. */
    public SshTransport(Host host, Function<Host, UserPrompts> requests) {
        this.host = host;
        this.requests = requests;
    }

    @Override public void onData(Consumer<String> cb) { data = cb; }

    @Override public void onState(BiConsumer<SessionStatus, String> cb) { state = cb; }

    /** Receives the name of the operating system read from the VPS, from the connection thread. */
    public void onOs(Consumer<String> cb) { os = cb; }

    @Override
    public void launch(int columns, int lines) {
        this.columns = columns;
        this.lines = lines;
        state.accept(SessionStatus.CONNECTING, null);
        Thread t = new Thread(this::connect, "ssh-" + host.name);
        t.setDaemon(true);
        t.start();
    }

    private void connect() {
        try {
            Session s = open(host, h -> new Prompts(h, requests.apply(h)), data, 15_000);
            session = s;

            ChannelShell c = (ChannelShell) s.openChannel("shell");
            int asked = columns, high = lines;
            c.setPtyType("xterm-256color", asked, high, 0, 0);
            InputStream in = c.getInputStream();
            output = c.getOutputStream();
            channel = c;
            c.connect(10_000);
            // Resized while the shell was being opened: those sizes were not sent, the last one goes now.
            if (columns != asked || lines != high) c.setPtySize(columns, lines, 0, 0);
            state.accept(SessionStatus.CONNECTED, null);
            readOs(s);

            byte[] buf = new byte[8192];
            Utf8 utf8 = new Utf8();
            int n;
            while ((n = in.read(buf)) >= 0) {
                if (n > 0) data.accept(utf8.decode(buf, n));
            }
            // The server ended the shell (exit, logout, a startup file that quits, a dropped connection): the
            // terminal says so, as ssh does, instead of staying there with a blinking cursor.
            if (!isClosed) data.accept(ended(c));
            close();
        } catch (JSchException | IOException | RuntimeException e) {
            // RuntimeException too: this thread is the terminal, it must always tell the user why it stopped.
            if (isClosed) return;
            String msg = SshErrors.explain(e), detail = SshErrors.technical(e);
            data.accept("\r\n\u001b[31mssh: " + msg + "\u001b[0m\r\n");
            // What JSch said, in grey: the sentence above may hide a detail that matters.
            if (!detail.equals(msg)) data.accept("\u001b[90m" + I18n.t("ssh.err.details", detail) + "\u001b[0m\r\n");
            isClosed = true;
            state.accept(SessionStatus.ERROR, msg);
            if (session != null) session.disconnect();
        }
    }

    /**
     * The grey line written when the server ends the shell, with its exit code when the server sends one; the
     * cursor is hidden, since nothing typed goes anywhere any more.
     */
    static String ended(ChannelShell c) {
        // The exit code comes with the end of the channel, a moment after the last data.
        for (int i = 0; i < 20 && !c.isClosed(); i++) {
            try {
                Thread.sleep(25);
            } catch (InterruptedException e) {
                Thread.currentThread().interrupt();
                break;
            }
        }
        int code = c.getExitStatus();
        String m = code >= 0 ? I18n.t("ssh.ended.code", String.valueOf(code)) : I18n.t("ssh.ended");
        return "\r\n\u001b[90m" + m + "\u001b[0m\r\n\u001b[?25l";
    }

    /** Told when its session has authenticated: a password just typed can be remembered. */
    public interface Authenticated {
        void authenticated();
    }

    /** Prompts that need the session they answer for (to recognise a changed host key). */
    public interface ForSession {
        void session(Session s);
    }

    /**
     * Connects to {@code host}, through its jump hosts ({@link Host#route}) if it has any: each hop is
     * connected first and the next server is reached through it, as {@code ssh -J} does. Every server gets
     * its own prompts and its own host key check. Disconnecting the returned session closes the whole chain.
     */
    public static Session open(Host host, Function<Host, UserInfo> prompts, Consumer<String> warnings, int timeout) throws JSchException, IOException {
        if (host.routeError != null) throw new JSchException(I18n.t("jump.err.route", host.routeError));
        Session previous = null;
        boolean connected = false;
        try {
            if (host.route != null) {
                for (Host hop : host.route) {
                    try {
                        previous = connect(hop, prompts.apply(hop), previous, warnings, timeout);
                    } catch (JSchException e) {
                        // Say which hop failed: "Auth fail" alone would point at the server at the end.
                        throw new SshErrors.Explained(I18n.t("jump.err.hop", hop.destination(), SshErrors.explain(e)), e);
                    }
                }
            }
            Session s = connect(host, prompts.apply(host), previous, warnings, timeout);
            connected = true;
            return s;
        } finally {
            // Whatever failed, the hops already open are closed with it.
            if (!connected && previous != null) previous.disconnect();
        }
    }

    private static Session connect(Host h, UserInfo ui, Session through, Consumer<String> warnings, int timeout) throws JSchException, IOException {
        Session s = prepare(h, ui, warnings);
        if (through != null) s.setProxy(new JumpProxy(through));
        s.connect(timeout);
        if (ui instanceof Authenticated a) a.authenticated();
        return s;
    }

    /**
     * Session ready to connect, with fingerprints from known_hosts and the keys
     * chosen for the host. Metrics collection uses it too, with prompts that
     * show no dialogs. {@code warnings} receives the messages for the user.
     */
    public static Session prepare(Host host, UserInfo questions, Consumer<String> warnings) throws JSchException, IOException {
        JSch jsch = new JSch();
        Path ssh = Path.of(System.getProperty("user.home"), ".ssh");
        Path known = ssh.resolve("known_hosts");
        // As ssh does: accepted fingerprints go into ~/.ssh/known_hosts.
        if (!Files.exists(known)) {
            Files.createDirectories(ssh);
            Files.createFile(known);
        }
        jsch.setKnownHosts(known.toString());
        jsch.setHostKeyRepository(new KnownKeys(jsch.getHostKeyRepository()));
        if (host.authMethod == Host.AuthMethod.KEY && host.key != null && !host.key.isBlank()) {
            Path k = Path.of(host.key).isAbsolute() ? Path.of(host.key) : ssh.resolve(host.key);
            if (Files.exists(k)) jsch.addIdentity(k.toString());
            else {
                warnings.accept("\u001b[33mssh: " + I18n.t("ssh.keyNotFound", k) + "\u001b[0m\r\n");
                if (!useAgent(jsch)) localKeys(jsch, ssh);
            }
        } else if (host.authMethod != Host.AuthMethod.PASSWORD) {
            // As ssh does: first the keys loaded in the agent, without an agent those in ~/.ssh.
            if (!useAgent(jsch)) localKeys(jsch, ssh);
        }
        Session s = jsch.getSession(host.user, host.address, host.port);
        s.setConfig("StrictHostKeyChecking", "ask");
        s.setUserInfo(questions);
        s.setServerAliveInterval(30_000);
        if (questions instanceof ForSession f) f.session(s);
        return s;
    }

    /** True if ssh-agent answers and has at least one key: then JSch signs through it. */
    private static boolean useAgent(JSch jsch) {
        if (System.getenv("SSH_AUTH_SOCK") == null) return false;
        try {
            AgentIdentityRepository agent = new AgentIdentityRepository(new SSHAgentConnector(new UnixDomainSocketFactory()));
            if (agent.getIdentities().isEmpty()) return false;
            jsch.setIdentityRepository(agent);
            return true;
        } catch (AgentProxyException | RuntimeException | LinkageError e) {
            // no agent, an agent that does not answer, or no Unix sockets in this JVM: the keys in ~/.ssh then
            return false;
        }
    }

    /** The default keys and then every other private key in ~/.ssh that has its .pub next to it. */
    private static void localKeys(JSch jsch, Path ssh) throws JSchException {
        List<Path> keys = new ArrayList<>();
        for (String n : new String[] {"id_ed25519", "id_ecdsa", "id_rsa"}) keys.add(ssh.resolve(n));
        try (var file = Files.list(ssh)) {
            file.filter(p -> p.toString().endsWith(".pub")).map(p -> Path.of(p.toString().replaceFirst("\\.pub$", "")))
                    .sorted().filter(p -> !keys.contains(p)).forEach(keys::add);
        } catch (IOException | UncheckedIOException e) {
            // Unreadable folder: the default keys remain.
        }
        for (Path k : keys) if (Files.isRegularFile(k)) jsch.addIdentity(k.toString());
    }

    /** On a separate channel of the same session, so there is no need to authenticate again. */
    private void readOs(Session s) {
        Thread t = new Thread(() -> {
            try {
                ChannelExec e = (ChannelExec) s.openChannel("exec");
                e.setCommand("cat /etc/os-release 2>/dev/null");
                InputStream in = e.getInputStream();
                e.connect(5_000);
                // A server that sends endless data must not exhaust the app's memory: os-release fits in a few lines.
                String text = readAtMost(in, 64 * 1024);
                e.disconnect();
                for (String line : text.split("\n")) {
                    if (line.startsWith("PRETTY_NAME=")) {
                        String v = line.substring(12).trim().replaceAll("^[\"']|[\"']$", "");
                        if (!v.isEmpty()) os.accept(v);
                    }
                }
            } catch (JSchException | IOException e) {
                // Optional information: without /etc/os-release the one typed by hand stays.
            }
        }, "ssh-os-" + host.name);
        t.setDaemon(true);
        t.start();
    }

    /**
     * SFTP channel on the same connection as the shell: no new login.
     * Call it outside the JavaFX thread; whoever opens it closes it.
     */
    public ChannelSftp openSftp() throws JSchException {
        Session s = session;
        if (s == null || !s.isConnected()) throw new JSchException(I18n.t("ssh.notReady"));
        ChannelSftp c = (ChannelSftp) s.openChannel("sftp");
        c.connect(10_000);
        return c;
    }

    @Override
    public void send(String text) {
        OutputStream o = output;
        if (o == null || isClosed) return;
        try {
            o.write(text.getBytes(StandardCharsets.UTF_8));
            o.flush();
        } catch (IOException e) {
            // Not in silence: the terminal would just stop answering.
            LOG.warning("Sending to " + host.name + " failed, the session is closed: " + e);
            data.accept("\r\n\u001b[31mssh: " + I18n.t("ssh.err.sendFailed", SshErrors.explain(e)) + "\u001b[0m\r\n");
            close();
        }
    }

    @Override
    public void resize(int columns, int lines) {
        this.columns = columns;
        this.lines = lines;
        ChannelShell c = channel;
        if (c != null && c.isConnected()) c.setPtySize(columns, lines, 0, 0);
    }

    @Override
    public void close() {
        if (isClosed) return;
        isClosed = true;
        if (channel != null) channel.disconnect();
        if (session != null) session.disconnect();
        state.accept(SessionStatus.CLOSED, null);
    }

    /** Many servers ask for the password as "keyboard-interactive": answer it like the normal one. */
    private class Prompts implements UserInfo, UIKeyboardInteractive, Authenticated, ForSession {
        private final Host host;
        private final UserPrompts requests;
        private Session session;
        private String password;
        private String passphrase;

        Prompts(Host host, UserPrompts requests) {
            this.host = host;
            this.requests = requests;
        }

        @Override public void session(Session s) { session = s; }

        @Override public void authenticated() { requests.authenticated(); }

        @Override public String getPassphrase() { return passphrase; }

        @Override public String getPassword() { return password; }

        @Override
        public boolean promptPassword(String message) {
            password = requests.password(message);
            return password != null;
        }

        @Override
        public boolean promptPassphrase(String message) {
            passphrase = requests.passphrase(message);
            return passphrase != null;
        }

        /**
         * New host: the user decides. CHANGED key (the case of a fake server in the middle): simply rejected,
         * as OpenSSH does; otherwise JSch would replace the key in ~/.ssh/known_hosts, which ssh uses too.
         */
        @Override
        public boolean promptYesNo(String message) {
            if (keyChanged(session)) {
                String item = host.port == 22 ? host.address : "[" + host.address + "]:" + host.port;
                data.accept("\u001b[1;31m" + I18n.t("ssh.changed.title", host.destination()) + "\u001b[0m\r\n"
                        + I18n.t("ssh.changed.refuse") + "\r\n"
                        + message.replace("\n", "\r\n") + "\r\n"
                        + I18n.t("ssh.changed.fix") + "\r\n"
                        + "    ssh-keygen -R '" + item + "'\r\n");
                return false;
            }
            return requests.confirm(message);
        }

        @Override public void showMessage(String message) { data.accept(message.replace("\n", "\r\n") + "\r\n"); }

        @Override
        public String[] promptKeyboardInteractive(String destination, String name, String instructions, String[] questions, boolean[] echo) {
            if (questions.length == 0) return new String[0];
            if (questions.length != 1 || echo[0]) return null;
            // The password (even the saved one) only if the question is the password: a 2FA code is answered by the user.
            if (!passwordQuestion(questions[0])) {
                String r = requests.reply(questions[0].trim());
                return r != null ? new String[] {r} : null;
            }
            return promptPassword(questions[0].trim()) ? new String[] {password} : null;
        }
    }

    /** True if, during the connection, the server showed a key different from the one in known_hosts. */
    static boolean keyChanged(Session s) {
        return s != null && s.getHostKeyRepository() instanceof KnownKeys n && n.changed();
    }

    /** Words that mean "password". */
    private static final Pattern PASSWORD =
            Pattern.compile("password|passwort|contraseña|mot de passe|senha|parola d'ordine");
    /**
     * Words of a one-time code, a PIN or a password change: even if the question says "password"
     * ("One-time password (OATH)", "OTP password", "New password"), the saved password does not fit there.
     */
    private static final Pattern NOT_PASSWORD = Pattern.compile(
            "one[- ]?time|\\botp\\b|\\btotp\\b|\\bhotp\\b|oath|token|code|passcode|codice|verifica|verification|\\bpin\\b"
                    + "|2fa|two[- ]factor|authenticator|yubikey|duo|\\bnew\\b|\\bnuova\\b|retype|re-enter|again|confirm|current");

    /**
     * A keyboard-interactive question that asks for the account password and nothing else. When in doubt,
     * no: then the user answers, and the silent metrics connection does not answer at all.
     */
    public static boolean passwordQuestion(String question) {
        // Without "user@host": a server called "code-server" does not make the question suspicious.
        String d = question.toLowerCase(Locale.ROOT).replaceAll("\\S*@\\S*", " ");
        return PASSWORD.matcher(d).find() && !NOT_PASSWORD.matcher(d).find();
    }

    /**
     * Reads a remote output to the end, but not beyond {@code max} bytes: more than that is a broken or
     * malicious server, and an error is raised instead of filling memory.
     */
    public static String readAtMost(InputStream in, int max) throws IOException {
        ByteArrayOutputStream all = new ByteArrayOutputStream();
        byte[] buf = new byte[8192];
        for (int n; (n = in.read(buf)) != -1; ) {
            if (all.size() + n > max) throw new IOException(I18n.t("ssh.err.outputTooLarge", max / 1024));
            all.write(buf, 0, n);
        }
        return all.toString(StandardCharsets.UTF_8);
    }

    /** UTF-8 decoding in pieces: a character split between two reads is completed at the next one. */
    static final class Utf8 {
        private final byte[] rest = new byte[4];
        private int nRest;

        String decode(byte[] buf, int n) {
            byte[] all = new byte[nRest + n];
            System.arraycopy(rest, 0, all, 0, nRest);
            System.arraycopy(buf, 0, all, nRest, n);
            int end = all.length;
            // Go back to the start of the last character and check whether it is complete.
            int i = end - 1;
            while (i >= 0 && i > end - 4 && (all[i] & 0xC0) == 0x80) i--;
            if (i >= 0) {
                int b = all[i] & 0xFF;
                int len = b >= 0xF0 ? 4 : b >= 0xE0 ? 3 : b >= 0xC0 ? 2 : 1;
                if (end - i < len) end = i;
            }
            nRest = all.length - end;
            System.arraycopy(all, end, rest, 0, nRest);
            return new String(all, 0, end, StandardCharsets.UTF_8);
        }
    }
}
