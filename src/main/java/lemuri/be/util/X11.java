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
import java.util.HashSet;
import java.util.List;
import java.util.Set;
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

    /** How long the app that owns the primary selection has to answer. */
    private static final long SELECTION_TIMEOUT_MS = 1000;
    /** A selection that fits in one property: longer ones come in pieces (the INCR protocol) and are left to AWT. */
    private static final int SELECTION_MAX_LONGS = 1 << 20;

    private final SocketChannel channel;
    private final int root;
    private final int idBase;

    private X11(SocketChannel channel, int root, int idBase) {
        this.channel = channel;
        this.root = root;
        this.idBase = idBase;
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

    /** The window of this process with that title, for {@link #moveResize(int, int, int, int, int)}; 0 if none. */
    public int window(String title) {
        try {
            return find(title, ProcessHandle.current().pid());
        } catch (IOException | RuntimeException e) {
            return 0;
        }
    }

    /**
     * Position and size in a single request, so the window manager changes them together: set one after the other,
     * the edge that should stay still moves first and comes back after. Without waiting for the answer. False if X fails.
     */
    public boolean moveResize(int window, int left, int top, int width, int height) {
        try {
            ByteBuffer b = buffer(28);
            // ConfigureWindow, with the value mask for x, y, width and height.
            b.put((byte) 12).put((byte) 0).putShort((short) 7).putInt(window).putShort((short) 0xF).putShort((short) 0)
                    .putInt(left).putInt(top).putInt(width).putInt(height);
            write(channel, b);
            return true;
        } catch (IOException | RuntimeException e) {
            return false;
        }
    }

    /** The position only, x and y in a single request, without waiting for the answer. False if X fails. */
    public boolean move(int window, int left, int top) {
        try {
            ByteBuffer b = buffer(20);
            // ConfigureWindow, with the value mask for x and y.
            b.put((byte) 12).put((byte) 0).putShort((short) 5).putInt(window).putShort((short) 0x3).putShort((short) 0)
                    .putInt(left).putInt(top);
            write(channel, b);
            return true;
        } catch (IOException | RuntimeException e) {
            return false;
        }
    }

    /** {@link #moveResize(int, int, int, int, int)} on the window with that title; false if it is not there or no X display. */
    public static boolean moveResize(String title, int left, int top, int width, int height) {
        String display = System.getenv("DISPLAY");
        if (display == null || Os.MAC) return false;
        try (X11 x = connect(display)) {
            int window = x.find(title, ProcessHandle.current().pid());
            if (window == 0 || !x.moveResize(window, left, top, width, height)) return false;
            // A round trip, so the request has surely left before the connection closes.
            x.atom("_NET_ACTIVE_WINDOW");
            return true;
        } catch (IOException | RuntimeException e) {
            return false;
        }
    }

    /**
     * Tells the window manager that the window of this process with that title, opened over another one, is a
     * dialog: JavaFX leaves it a normal window, and a dock then counts it as one more window of the app.
     * False if it is not on the screen (yet) or there is no X display.
     */
    public static boolean markDialog(String title) {
        return markDialogs(title);
    }

    /**
     * The same for the windows of this process created over another one and not on the screen yet, which have no
     * title so far. This is the moment that counts: GNOME's dock counts the windows of an app when they appear.
     */
    public static boolean markUnshownDialogs() {
        return markDialogs(null);
    }

    private static boolean markDialogs(String title) {
        String display = System.getenv("DISPLAY");
        if (display == null || Os.MAC) return false;
        try (X11 x = connect(display)) {
            int wmPid = x.atom("_NET_WM_PID"), wmName = x.atom("_NET_WM_NAME");
            int transientFor = x.atom("WM_TRANSIENT_FOR"), type = x.atom("_NET_WM_WINDOW_TYPE");
            int dialog = x.atom("_NET_WM_WINDOW_TYPE_DIALOG");
            long pid = ProcessHandle.current().pid();
            // The window manager lists the windows on the screen; one not shown yet is only a child of the root.
            int[] shown = ints(x.property(x.root, x.atom("_NET_CLIENT_LIST")));
            int[] windows = shown;
            if (title == null) {
                Set<Integer> listed = new HashSet<>();
                for (int w : shown) listed.add(w);
                windows = Arrays.stream(x.children(x.root)).filter(w -> !listed.contains(w)).toArray();
            }
            boolean found = false;
            for (int w : windows) {
                try {
                    int[] p = ints(x.property(w, wmPid));
                    if (p.length != 1 || Integer.toUnsignedLong(p[0]) != pid) continue;
                    if (title != null && !new String(x.property(w, wmName), StandardCharsets.UTF_8).equals(title)) continue;
                    // Only windows that belong to another one: the main window may have the same title.
                    if (ints(x.property(w, transientFor)).length != 1) continue;
                } catch (IOException gone) {
                    // A window of another program closed in the meantime.
                    continue;
                }
                // ChangeProperty, replacing the value with one atom (type ATOM is 4).
                ByteBuffer b = buffer(28);
                b.put((byte) 18).put((byte) 0).putShort((short) 7).putInt(w).putInt(type).putInt(4)
                        .put((byte) 32).put((byte) 0).putShort((short) 0).putInt(1).putInt(dialog);
                write(x.channel, b);
                found = true;
            }
            // A round trip, so the requests have surely left before the connection closes.
            x.atom("_NET_ACTIVE_WINDOW");
            return found;
        } catch (IOException | RuntimeException e) {
            return false;
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

    /**
     * The text of the primary selection, the one the middle button pastes. Null if it cannot be told here: no X
     * display, no owner, an owner that does not answer in time, a text too long for one property. AWT reads it
     * too, but takes about a quarter of a second at each paste, and longer the first time.
     */
    public static String primaryText() {
        String display = System.getenv("DISPLAY");
        if (display == null || Os.MAC) return null;
        try (X11 x = connect(display)) {
            Thread limit = new Thread(() -> {
                try {
                    Thread.sleep(SELECTION_TIMEOUT_MS);
                    // Closing the connection wakes up the read that is waiting for the owner.
                    x.close();
                } catch (InterruptedException | IOException e) {
                    // The answer came in time, or the connection is already closed.
                }
            }, "primary-selection-limit");
            limit.setDaemon(true);
            limit.start();
            try {
                return x.convertPrimary();
            } finally {
                limit.interrupt();
            }
        } catch (IOException | RuntimeException e) {
            LOG.fine("Cannot read the primary selection through X11: " + e);
            return null;
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
            return new X11(c, root, setup.getInt(4));
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

    /** The windows directly inside {@code window} (QueryTree). */
    private int[] children(int window) throws IOException {
        ByteBuffer b = buffer(8);
        b.put((byte) 15).put((byte) 0).putShort((short) 2).putInt(window);
        write(channel, b);
        ByteBuffer r = reply();
        int[] out = new int[Math.min(Short.toUnsignedInt(r.getShort(16)), (r.capacity() - 32) / 4)];
        for (int i = 0; i < out.length; i++) out[i] = r.getInt(32 + 4 * i);
        return out;
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
        return value(propertyReply(window, property, 16_384));
    }

    private ByteBuffer propertyReply(int window, int property, int maxLongs) throws IOException {
        ByteBuffer b = buffer(24);
        b.put((byte) 20).put((byte) 0).putShort((short) 6).putInt(window).putInt(property).putInt(0).putInt(0).putInt(maxLongs);
        write(channel, b);
        return reply();
    }

    private static byte[] value(ByteBuffer r) {
        int format = Byte.toUnsignedInt(r.get(1));
        int length = r.getInt(16) * Math.max(1, format / 8);
        byte[] v = new byte[Math.max(0, Math.min(length, r.capacity() - 32))];
        r.position(32);
        r.get(v);
        return v;
    }

    /** Asks the owner of the selection to write it as UTF-8 in a property of a window made for that, and reads it. */
    private String convertPrimary() throws IOException {
        int primary = atom("PRIMARY"), utf8 = atom("UTF8_STRING"), target = atom("LEMURI_PRIMARY"), incr = atom("INCR");
        int window = idBase;
        ByteBuffer b = buffer(32);
        // CreateWindow: child of the root, 1x1, InputOnly, nothing to configure. It goes away with the connection.
        b.put((byte) 1).put((byte) 0).putShort((short) 8).putInt(window).putInt(root)
                .putShort((short) 0).putShort((short) 0).putShort((short) 1).putShort((short) 1)
                .putShort((short) 0).putShort((short) 2).putInt(0).putInt(0);
        write(channel, b);
        b = buffer(24);
        // ConvertSelection, at the current time.
        b.put((byte) 24).put((byte) 0).putShort((short) 6).putInt(window).putInt(primary).putInt(utf8).putInt(target).putInt(0);
        write(channel, b);
        while (true) {
            ByteBuffer event = read(channel, 32);
            int kind = event.get(0) & 0x7f;
            if (kind == 0) throw new IOException("X11 error " + Byte.toUnsignedInt(event.get(1)));
            if (kind != 31) continue;
            // SelectionNotify: the property at 20, none if the owner cannot give that format (or there is no owner).
            if (event.getInt(20) == 0) return null;
            ByteBuffer r = propertyReply(window, target, SELECTION_MAX_LONGS);
            if (r.getInt(8) == incr || r.getInt(12) != 0) return null;
            return new String(value(r), StandardCharsets.UTF_8);
        }
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
