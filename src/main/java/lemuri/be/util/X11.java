package lemuri.be.util;

import java.io.IOException;
import java.net.StandardProtocolFamily;
import java.net.UnixDomainSocketAddress;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.nio.channels.SocketChannel;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.Arrays;
import java.util.List;
import java.util.logging.Logger;

/**
 * The little of X11 that JavaFX does not offer. First: brings a window of this app to the front and gives it the keyboard, even when the request does not come from
 * a click in the app (the F11 system shortcut). GNOME refuses that to the app itself: the window opens behind
 * the others and a "Lemuri is ready" notification appears instead. A pager (dock, task list) is allowed to do it,
 * through the {@code _NET_ACTIVE_WINDOW} message to the root window: this class speaks just enough of the X11
 * protocol to send it, with no native library. JavaFX draws through X11 (XWayland on Wayland), so it works on
 * both; without an X display it does nothing. Second: where a window really is on the screen, which JavaFX
 * may not know when the window manager places it again on its own.
 */
public final class X11 implements AutoCloseable {
    private static final Logger LOG = Log.of(X11.class);

    private final SocketChannel channel;
    private final int root;

    private X11(SocketChannel channel, int root) {
        this.channel = channel;
        this.root = root;
    }

    /**
     * Activates the mapped window of this process with that title. Returns false if there is no X display or
     * no such window (yet: right after show() the window manager may still be mapping it).
     */
    public static boolean activate(String title) {
        String display = System.getenv("DISPLAY");
        if (display == null || Os.MAC) return false;
        try (X11 x = connect(display)) {
            int window = x.find(title, ProcessHandle.current().pid());
            if (window == 0) return false;
            x.sendActivate(window);
            return true;
        } catch (IOException | RuntimeException e) {
            LOG.fine("Cannot activate the window through X11: " + e);
            return false;
        }
    }

    /**
     * A connection that follows the pointer, for as long as a drag lasts: while the app resizes its own window
     * the drag events may stop coming, so the pointer is asked directly. Null without X.
     */
    public static X11 pointerTracker() {
        String display = System.getenv("DISPLAY");
        if (display == null || Os.MAC) return null;
        try {
            return connect(display);
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    /** Where the pointer is on the screen and whether the left button is down: {x, y, 1 or 0}; null if X fails. */
    public int[] pointer() {
        try {
            ByteBuffer b = buffer(8);
            b.put((byte) 38).put((byte) 0).putShort((short) 2).putInt(root);
            write(channel, b);
            ByteBuffer r = reply();
            // QueryPointer: root-x and root-y at 16 and 18, the button and modifier mask at 24 (Button1 = 0x100).
            return new int[] {r.getShort(16), r.getShort(18), (Short.toUnsignedInt(r.getShort(24)) & 0x100) != 0 ? 1 : 0};
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    /** The X id of the mapped window of this process with that title, or 0 if there is none or no X display. */
    public static int windowId(String title) {
        String display = System.getenv("DISPLAY");
        if (display == null || Os.MAC) return 0;
        try (X11 x = connect(display)) {
            return x.find(title, ProcessHandle.current().pid());
        } catch (IOException | RuntimeException e) {
            return 0;
        }
    }

    /** True if the window of this process with that title has the focus; false also when it cannot tell. */
    public static boolean isActive(String title) {
        String display = System.getenv("DISPLAY");
        if (display == null || Os.MAC) return false;
        try (X11 x = connect(display)) {
            int window = x.find(title, ProcessHandle.current().pid());
            int[] active = ints(x.property(x.root, x.atom("_NET_ACTIVE_WINDOW")));
            return window != 0 && active.length == 1 && active[0] == window;
        } catch (IOException | RuntimeException e) {
            return false;
        }
    }

    /** Position and size on the screen of the window of this process with that title, or null if unknown. */
    public static int[] bounds(String title) {
        String display = System.getenv("DISPLAY");
        if (display == null || Os.MAC) return null;
        try (X11 x = connect(display)) {
            int window = x.find(title, ProcessHandle.current().pid());
            return window == 0 ? null : x.geometry(window);
        } catch (IOException | RuntimeException e) {
            return null;
        }
    }

    /**
     * The usable area of each monitor, {x, y, width, height} in X pixels, as GNOME computes it
     * ({@code _GTK_WORKAREAS_D<desktop>}): with more monitors {@code _NET_WORKAREA}, where JavaFX looks, is one
     * rectangle for the whole desktop and misses a dock or a bar that is on one monitor only. Empty if not known.
     */
    public static List<int[]> workAreas() {
        String display = System.getenv("DISPLAY");
        if (display == null || Os.MAC) return List.of();
        try (X11 x = connect(display)) {
            int[] desktop = ints(x.property(x.root, x.atom("_NET_CURRENT_DESKTOP")));
            int[] v = ints(x.property(x.root, x.atom("_GTK_WORKAREAS_D" + (desktop.length == 1 ? desktop[0] : 0))));
            List<int[]> out = new ArrayList<>();
            for (int i = 0; i + 3 < v.length; i += 4) out.add(Arrays.copyOfRange(v, i, i + 4));
            return out;
        } catch (IOException | RuntimeException e) {
            return List.of();
        }
    }

    // ── Connection ───────────────────────────────────────────────────────────

    /** Only local displays (":0", ":1.0"): the socket in /tmp/.X11-unix, with the cookie of $XAUTHORITY. */
    private static X11 connect(String display) throws IOException {
        if (!display.startsWith(":")) throw new IOException("not a local display: " + display);
        String number = display.substring(1).split("\\.")[0];
        SocketChannel c = SocketChannel.open(StandardProtocolFamily.UNIX);
        try {
            c.connect(UnixDomainSocketAddress.of("/tmp/.X11-unix/X" + number));
            byte[][] cookie = cookie(number);
            byte[] name = cookie != null ? cookie[0] : new byte[0], data = cookie != null ? cookie[1] : new byte[0];
            ByteBuffer b = buffer(12 + pad(name.length) + pad(data.length));
            b.put((byte) 'l').put((byte) 0).putShort((short) 11).putShort((short) 0)
                    .putShort((short) name.length).putShort((short) data.length).putShort((short) 0);
            b.put(name).position(12 + pad(name.length));
            b.put(data);
            write(c, b);
            ByteBuffer head = read(c, 8);
            if (head.get(0) != 1) throw new IOException("the X server refused the connection");
            ByteBuffer setup = read(c, 4 * Short.toUnsignedInt(head.getShort(6)));
            int vendor = Short.toUnsignedInt(setup.getShort(16));
            int formats = Byte.toUnsignedInt(setup.get(21));
            // After the fixed part: the vendor name, the pixmap formats, then the screens; the first field of a screen is its root.
            int root = setup.getInt(32 + pad(vendor) + 8 * formats);
            return new X11(c, root);
        } catch (IOException | RuntimeException e) {
            c.close();
            throw e;
        }
    }

    /** Name and data of the MIT-MAGIC-COOKIE-1 for this display in the Xauthority file, or null to try without. */
    private static byte[][] cookie(String number) throws IOException {
        String file = System.getenv("XAUTHORITY");
        Path p = file != null ? Path.of(file) : Path.of(System.getProperty("user.home"), ".Xauthority");
        if (!Files.isReadable(p)) return null;
        ByteBuffer b = ByteBuffer.wrap(Files.readAllBytes(p)).order(ByteOrder.BIG_ENDIAN);
        while (b.remaining() >= 2) {
            b.getShort(); // family: local or wild, both fine for the local socket
            field(b); // address
            String n = new String(field(b), StandardCharsets.US_ASCII);
            byte[] name = field(b), data = field(b);
            if ((n.isEmpty() || n.equals(number)) && new String(name, StandardCharsets.US_ASCII).equals("MIT-MAGIC-COOKIE-1"))
                return new byte[][] {name, data};
        }
        return null;
    }

    private static byte[] field(ByteBuffer b) {
        byte[] v = new byte[Short.toUnsignedInt(b.getShort())];
        b.get(v);
        return v;
    }

    // ── Requests ─────────────────────────────────────────────────────────────

    /** The window in the window manager's list that belongs to this process and has that title; 0 if none. */
    private int find(String title, long pid) throws IOException {
        int clientList = atom("_NET_CLIENT_LIST"), wmPid = atom("_NET_WM_PID"), wmName = atom("_NET_WM_NAME");
        for (int w : ints(property(root, clientList))) {
            int[] p = ints(property(w, wmPid));
            if (p.length != 1 || Integer.toUnsignedLong(p[0]) != pid) continue;
            if (new String(property(w, wmName), StandardCharsets.UTF_8).equals(title)) return w;
        }
        return 0;
    }

    /** _NET_ACTIVE_WINDOW as a pager sends it (source 2): the window manager raises it and gives it the focus. */
    private void sendActivate(int window) throws IOException {
        int active = atom("_NET_ACTIVE_WINDOW");
        ByteBuffer b = buffer(44);
        b.put((byte) 25).put((byte) 0).putShort((short) 11).putInt(root)
                .putInt(0x180000); // SubstructureRedirect | SubstructureNotify
        b.put((byte) 33).put((byte) 32).putShort((short) 0).putInt(window).putInt(active)
                .putInt(2).putInt(0).putInt(0).putInt(0).putInt(0);
        write(channel, b);
        // A round trip, so the message has surely left before the connection closes.
        atom("_NET_ACTIVE_WINDOW");
    }

    /** x, y (from the top left of the screen), width and height of the window. */
    private int[] geometry(int window) throws IOException {
        ByteBuffer b = buffer(8);
        b.put((byte) 14).put((byte) 0).putShort((short) 2).putInt(window);
        write(channel, b);
        ByteBuffer size = reply();
        // The position in GetGeometry is relative to the parent (the frame of the window manager): translate to the root.
        b = buffer(16);
        b.put((byte) 40).put((byte) 0).putShort((short) 4).putInt(window).putInt(root).putShort((short) 0).putShort((short) 0);
        write(channel, b);
        ByteBuffer where = reply();
        return new int[] {where.getShort(12), where.getShort(14), Short.toUnsignedInt(size.getShort(16)), Short.toUnsignedInt(size.getShort(18))};
    }

    private int atom(String name) throws IOException {
        byte[] n = name.getBytes(StandardCharsets.US_ASCII);
        ByteBuffer b = buffer(8 + pad(n.length));
        b.put((byte) 16).put((byte) 0).putShort((short) (2 + pad(n.length) / 4)).putShort((short) n.length).putShort((short) 0).put(n);
        write(channel, b);
        return reply().getInt(8);
    }

    /** The value of a property (any type, up to 64 KB), or nothing if it is not set. */
    private byte[] property(int window, int property) throws IOException {
        ByteBuffer b = buffer(24);
        b.put((byte) 20).put((byte) 0).putShort((short) 6).putInt(window).putInt(property).putInt(0).putInt(0).putInt(16_384);
        write(channel, b);
        ByteBuffer r = reply();
        int format = Byte.toUnsignedInt(r.get(1));
        int length = r.getInt(16) * Math.max(1, format / 8);
        byte[] v = new byte[Math.max(0, Math.min(length, r.capacity() - 32))];
        r.position(32);
        r.get(v);
        return v;
    }

    private static int[] ints(byte[] v) {
        ByteBuffer b = ByteBuffer.wrap(v).order(ByteOrder.LITTLE_ENDIAN);
        List<Integer> out = new ArrayList<>();
        while (b.remaining() >= 4) out.add(b.getInt());
        return out.stream().mapToInt(Integer::intValue).toArray();
    }

    /** The next reply, skipping events; an error (a window closed in the meantime) becomes an exception. */
    private ByteBuffer reply() throws IOException {
        while (true) {
            ByteBuffer head = read(channel, 32);
            int kind = head.get(0);
            if (kind == 0) throw new IOException("X11 error " + Byte.toUnsignedInt(head.get(1)));
            if (kind != 1) continue;
            int extra = 4 * head.getInt(4);
            if (extra == 0) return head;
            ByteBuffer all = buffer(32 + extra);
            all.put(head.rewind()).put(read(channel, extra));
            return all.rewind();
        }
    }

    // ── Bytes ────────────────────────────────────────────────────────────────

    private static ByteBuffer buffer(int size) {
        return ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN);
    }

    private static int pad(int n) {
        return (n + 3) & ~3;
    }

    private static void write(SocketChannel c, ByteBuffer b) throws IOException {
        b.rewind();
        while (b.hasRemaining()) c.write(b);
    }

    private static ByteBuffer read(SocketChannel c, int n) throws IOException {
        ByteBuffer b = buffer(n);
        while (b.hasRemaining()) if (c.read(b) < 0) throw new IOException("X server closed the connection");
        return b.rewind();
    }

    @Override
    public void close() throws IOException {
        channel.close();
    }
}
