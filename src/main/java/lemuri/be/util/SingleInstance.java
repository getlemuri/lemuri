package lemuri.be.util;

import java.io.BufferedReader;
import java.io.IOException;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.InetSocketAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.channels.OverlappingFileLockException;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
import java.security.SecureRandom;
import java.util.HexFormat;
import java.util.function.Consumer;
import java.util.logging.Logger;

/**
 * One copy of the app per user. Two copies would each keep their own vault and hosts in memory and,
 * when saving, would erase what the other wrote; on top of that two tray icons and two local network
 * listeners.
 *
 * <p>The first copy takes the lock on {@code instance.lock} and listens on 127.0.0.1, with port and secret
 * code in {@code instance.port} (readable by the user only). A later copy finds the lock taken, sends
 * the code to the first one, which shows itself, and quits. After the code may come a command, separated by
 * a space: {@code toggle} (from {@code --toggle}, the F11 system shortcut) shows or hides the terminal.
 */
public final class SingleInstance {
    private static final Logger LOG = Log.of(SingleInstance.class);
    /** What a later copy asks for when started with no command: to show the window. */
    public static final String SHOW = "show";
    public static final String TOGGLE = "toggle";

    private final FileChannel channel;
    private final FileLock block;
    private final ServerSocket listening;

    private SingleInstance(FileChannel channel, FileLock block, ServerSocket listening) {
        this.channel = channel;
        this.block = block;
        this.listening = listening;
    }

    /**
     * Tries to be the working copy. Returns null if there is already another one: in that case it has
     * already been asked to show itself, and the caller must quit. {@code callback} runs (from a network
     * thread) every time a later copy asks to show the window.
     */
    public static SingleInstance acquire(Path folder, Runnable callback) {
        return acquire(folder, SHOW, c -> {
            if (c.equals(SHOW)) callback.run();
        });
    }

    /**
     * Like {@link #acquire(Path, Runnable)}, but if another copy is running this one asks it for {@code command};
     * {@code onCommand} receives (from a network thread) what later copies ask for: {@link #SHOW} or {@link #TOGGLE}.
     */
    public static SingleInstance acquire(Path folder, String command, Consumer<String> onCommand) {
        Path lock = folder.resolve("instance.lock");
        Path port = folder.resolve("instance.port");
        FileChannel c = null;
        try {
            PrivateFiles.folder(folder);
            c = FileChannel.open(lock, StandardOpenOption.CREATE, StandardOpenOption.WRITE);
            FileLock b;
            try {
                b = c.tryLock();
            } catch (OverlappingFileLockException alreadyHere) {
                b = null; // already taken by this same JVM
            }
            if (b == null) {
                c.close();
                call(port, command);
                return null;
            }
            ServerSocket s = new ServerSocket();
            s.bind(new InetSocketAddress(InetAddress.getLoopbackAddress(), 0));
            byte[] random = new byte[16];
            new SecureRandom().nextBytes(random);
            String code = HexFormat.of().formatHex(random);
            PrivateFiles.write(port, s.getLocalPort() + " " + code + "\n");
            SingleInstance i = new SingleInstance(c, b, s);
            Thread t = new Thread(() -> i.listen(code, onCommand), "single-instance");
            t.setDaemon(true);
            t.start();
            return i;
        } catch (IOException e) {
            // A file system without locks must not prevent using the app: go on without protection.
            LOG.warning("Cannot check for another running copy of Lemuri: " + e.getMessage());
            if (c != null) {
                try {
                    c.close();
                } catch (IOException ignored) {
                    // nothing to do
                }
            }
            return new SingleInstance(null, null, null);
        }
    }

    private void listen(String code, Consumer<String> onCommand) {
        while (!listening.isClosed()) {
            try (Socket s = listening.accept()) {
                s.setSoTimeout(2_000);
                String line = new BufferedReader(new InputStreamReader(s.getInputStream(), StandardCharsets.US_ASCII)).readLine();
                if (line == null) continue;
                // Only the two known lines: anything else, even after the right code, is ignored.
                if (line.equals(code)) onCommand.accept(SHOW);
                else if (line.equals(code + " " + TOGGLE)) onCommand.accept(TOGGLE);
            } catch (IOException e) {
                // a copy that closed at once, wrong code or closed socket: go on
            }
        }
    }

    /** Asks the working copy to show itself (or to run the command). If it is still starting, the port arrives shortly: retry. */
    private static void call(Path port, String command) {
        for (int test = 0; test < 20; test++) {
            try {
                String[] p = Files.readString(port).trim().split(" ");
                try (Socket s = new Socket()) {
                    s.connect(new InetSocketAddress(InetAddress.getLoopbackAddress(), Integer.parseInt(p[0])), 1_000);
                    OutputStream o = s.getOutputStream();
                    o.write((p[1] + (command.equals(SHOW) ? "" : " " + command) + "\n").getBytes(StandardCharsets.US_ASCII));
                    o.flush();
                    return;
                }
            } catch (IOException | RuntimeException e) {
                try {
                    Thread.sleep(250);
                } catch (InterruptedException end) {
                    Thread.currentThread().interrupt();
                    return;
                }
            }
        }
        LOG.warning("Another copy of Lemuri is running but did not answer");
    }

    /** Gives up the slot (on exit, and in tests). */
    public void release() {
        try {
            if (listening != null) listening.close();
            if (block != null) block.release();
            if (channel != null) channel.close();
        } catch (IOException e) {
            // does not matter on exit
        }
    }
}
