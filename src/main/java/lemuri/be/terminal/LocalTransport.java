package lemuri.be.terminal;

import com.pty4j.PtyProcess;
import com.pty4j.PtyProcessBuilder;
import com.pty4j.WinSize;
import java.io.File;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.nio.charset.StandardCharsets;
import java.util.HashMap;
import java.util.Map;
import java.util.function.BiConsumer;
import java.util.function.Consumer;
import lemuri.be.dto.SessionStatus;
import lemuri.be.service.Transport;
import lemuri.be.util.I18n;
import lemuri.be.util.Os;

/**
 * The shell of this computer (the user's $SHELL, else bash, or zsh on a Mac) in a real pseudo-terminal
 * (pty4j): colours, full-screen programs and resizing work as in any terminal.
 */
public final class LocalTransport implements Transport {
    private volatile Consumer<String> data = d -> {};
    private volatile BiConsumer<SessionStatus, String> state = (s, m) -> {};
    private volatile PtyProcess process;
    private volatile OutputStream out;
    private volatile boolean closed;
    /** The last size asked for: a resize that arrives while the shell is still starting is applied once it runs. */
    private volatile int columns, lines;

    @Override
    public void onData(Consumer<String> cb) {
        data = cb;
    }

    @Override
    public void onState(BiConsumer<SessionStatus, String> cb) {
        state = cb;
    }

    @Override
    public void launch(int columns, int lines) {
        this.columns = columns;
        this.lines = lines;
        state.accept(SessionStatus.CONNECTING, null);
        Thread t = new Thread(() -> run(columns, lines), "local-shell");
        t.setDaemon(true);
        t.start();
    }

    private void run(int columns, int lines) {
        try {
            Map<String, String> env = new HashMap<>(System.getenv());
            env.put("TERM", "xterm-256color");
            env.put("COLORTERM", "truecolor");
            String own = System.getenv("SHELL");
            String shell = own != null && new File(own).canExecute() ? own : Os.MAC ? "/bin/zsh" : "/bin/bash";
            PtyProcess p = new PtyProcessBuilder(new String[] {shell, "-l"})
                    .setEnvironment(env)
                    .setDirectory(System.getProperty("user.home"))
                    .setInitialColumns(columns)
                    .setInitialRows(lines)
                    .start();
            process = p;
            if (this.columns != columns || this.lines != lines) p.setWinSize(new WinSize(this.columns, this.lines));
            out = p.getOutputStream();
            state.accept(SessionStatus.CONNECTED, null);

            InputStream in = p.getInputStream();
            byte[] buf = new byte[8192];
            SshTransport.Utf8 utf8 = new SshTransport.Utf8();
            for (int n; (n = in.read(buf)) >= 0; ) {
                if (n > 0) data.accept(utf8.decode(buf, n));
            }
            close();
        } catch (IOException e) {
            if (closed) return;
            String msg = e.getMessage() != null ? e.getMessage() : e.toString();
            data.accept("\r\n\u001b[31m" + I18n.t("terminal.localError", msg) + "\u001b[0m\r\n");
            closed = true;
            state.accept(SessionStatus.ERROR, msg);
        }
    }

    @Override
    public void send(String d) {
        OutputStream o = out;
        if (o == null || closed) return;
        try {
            o.write(d.getBytes(StandardCharsets.UTF_8));
            o.flush();
        } catch (IOException e) {
            close();
        }
    }

    @Override
    public void resize(int columns, int lines) {
        this.columns = columns;
        this.lines = lines;
        PtyProcess p = process;
        if (p != null && p.isAlive()) p.setWinSize(new WinSize(columns, lines));
    }

    @Override
    public void close() {
        if (closed) return;
        closed = true;
        PtyProcess p = process;
        if (p != null) p.destroy();
        state.accept(SessionStatus.CLOSED, null);
    }
}
