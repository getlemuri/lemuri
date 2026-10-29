package lemuri.fe.v.terminal;

import java.util.Collections;
import java.util.List;
import java.util.Set;
import java.util.WeakHashMap;
import java.util.function.Consumer;
import java.util.prefs.Preferences;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import javafx.animation.PauseTransition;
import javafx.application.Platform;
import javafx.beans.property.BooleanProperty;
import javafx.beans.property.ReadOnlyBooleanProperty;
import javafx.beans.property.SimpleBooleanProperty;
import javafx.beans.value.ChangeListener;
import javafx.beans.value.ObservableBooleanValue;
import javafx.beans.value.ObservableValue;
import javafx.concurrent.Worker;
import javafx.geometry.Point2D;
import javafx.scene.Scene;
import javafx.scene.control.Tooltip;
import javafx.scene.input.Clipboard;
import javafx.scene.input.ClipboardContent;
import javafx.scene.input.KeyCode;
import javafx.scene.input.KeyEvent;
import javafx.scene.input.MouseButton;
import javafx.scene.input.MouseEvent;
import javafx.scene.layout.StackPane;
import javafx.scene.paint.Color;
import javafx.scene.web.WebView;
import javafx.util.Duration;
import netscape.javascript.JSObject;
import lemuri.be.service.Transport;

/**
 * xterm.js terminal inside a WebView, connected to a {@link Transport}.
 * The node moves between the host console views (another host's console,
 * the console page opened again) without losing its screen or scrollback.
 */
public final class Terminal extends StackPane {
    private final WebView web = new WebView();
    private final Transport transport;
    /** Where what the user types goes: this terminal's shell, unless {@link #onInput} sends it elsewhere too. */
    private Consumer<String> input;
    // Mandatory strong reference: the WebView holds Java bridges weakly.
    private final Bridge bridge = new Bridge();
    private final StringBuilder pending = new StringBuilder();
    private JSObject window;
    private boolean ready;
    /**
     * Invisible signal (an OSC sequence xterm would ignore) printed by the shell when a hidden command
     * starts: everything that arrives before it is the echo of the command, and is not shown.
     */
    private static final String MARK = "\u001b]777;lemuri\u0007";
    private boolean hiding;
    private final StringBuilder hidden = new StringBuilder();
    /** Shell reply to {@link #currentFolder}: another OSC sequence, with the path inside. */
    private static final String DIR = "\u001b]777;lemuri-pwd;";
    private Consumer<String> onFolder;
    private final StringBuilder reply = new StringBuilder();
    private final BooleanProperty ghost = new SimpleBooleanProperty();
    /** Font size, the same for every terminal and kept between runs; Ctrl+plus, Ctrl+minus and Ctrl+0 change it. */
    private static final String FONT_SIZE = "terminalFontSize";
    private static final int MIN_FONT = 8, MAX_FONT = 32, DEFAULT_FONT = 13;
    private static final Preferences PREF = Preferences.userRoot().node("lemuri/ui");
    private static final String TYPES_COMMANDS = "terminalCommands";
    private static int fontSize = Math.max(MIN_FONT, Math.min(MAX_FONT, PREF.getInt(FONT_SIZE, DEFAULT_FONT)));
    /**
     * What the user types or pastes goes here instead of straight to the shell (a synced tab sends it to all
     * its parts). The commands Lemuri types itself (double click, uploads) still go only to this shell.
     */
    public void onInput(Consumer<String> c) {
        input = c;
    }

    /** The open terminals, so a new size reaches all of them. */
    private static final Set<Terminal> ALL = Collections.newSetFromMap(new WeakHashMap<>());

    public Terminal(Transport transport) {
        this.transport = transport;
        this.input = transport::send;
        getStyleClass().add("terminal-screen");
        web.setContextMenuEnabled(false);
        getChildren().add(web);
        ALL.add(this);
        // A filter, so xterm never sees these keys (Ctrl+minus would otherwise send ^_ to the shell). Copy and paste
        // as in the Linux terminals: Ctrl+Shift+C, Ctrl+Shift+V, Shift+Insert; Ctrl+C stays the interrupt.
        addEventFilter(KeyEvent.KEY_PRESSED, e -> {
            KeyCode k = e.getCode();
            if (e.isShortcutDown() && e.isShiftDown() && !e.isAltDown() && k == KeyCode.C) {
                copy();
                e.consume();
                return;
            }
            if (e.isShortcutDown() && e.isShiftDown() && !e.isAltDown() && k == KeyCode.V
                    || e.isShiftDown() && !e.isShortcutDown() && k == KeyCode.INSERT) {
                pasteClipboard();
                e.consume();
                return;
            }
            if (!e.isShortcutDown() || e.isAltDown()) return;
            int change = zoom(e);
            if (change == Integer.MIN_VALUE) return;
            e.consume();
            setFontSize(change == 0 ? DEFAULT_FONT : fontSize + change);
        });
        // The middle button pastes the primary selection, as in every Linux terminal.
        addEventFilter(MouseEvent.MOUSE_PRESSED, e -> {
            if (e.getButton() != MouseButton.MIDDLE) return;
            e.consume();
            PrimarySelection.get(text -> paste(text != null ? text : clipboardText()));
        });
        addEventFilter(MouseEvent.MOUSE_RELEASED, e -> {
            if (e.getButton() == MouseButton.MIDDLE) e.consume();
        });
        // JavaFX focus on the WebView is not enough: xterm must know too, or it does not type and the cursor does not blink.
        web.focusedProperty().addListener((o, before, now) -> {
            if (now && ready) window.call("focus");
        });

        transport.onData(d -> Platform.runLater(() -> write(d)));

        web.getEngine().getLoadWorker().stateProperty().addListener((o, before, now) -> {
            if (now != Worker.State.SUCCEEDED) return;
            window = (JSObject) web.getEngine().executeScript("window");
            window.setMember("java", bridge);
            web.getEngine().executeScript("start(" + fontSize + ")");
        });
        web.getEngine().load(Terminal.class.getResource("/lemuri/terminal/terminal.html").toExternalForm());
    }

    /** Interactive mode: the same switch in Settings and on every terminal, kept between runs. */
    private static final BooleanProperty INTERACTIVE = new SimpleBooleanProperty(PREF.getBoolean(TYPES_COMMANDS, true));

    /**
     * The user lets Lemuri type commands into the terminal: interactive mode (double click on a name or on the
     * empty space at the prompt, uploads into the shell's folder).
     */
    public static boolean typesCommands() {
        return INTERACTIVE.get();
    }

    public static void typesCommands(boolean yes) {
        INTERACTIVE.set(yes);
        PREF.putBoolean(TYPES_COMMANDS, yes);
    }

    /** The folder an {@code ls} was asked to list, if it was given exactly one; null for the shell's own. */
    public static String lsFolder(String command) {
        if (command == null) return null;
        // Words as the shell splits them, a quoted one being a single word ('my files').
        // "cd app && ls" (the double click on a folder): the ls part, which runs in the folder just entered.
        Matcher m = Pattern.compile("'[^']*'|\"[^\"]*\"|\\S+").matcher(command.trim().replaceFirst("^cd\\s.*?&&\\s*", ""));
        String only = null;
        for (boolean first = true; m.find(); first = false) {
            String w = m.group();
            if (first || w.startsWith("-")) continue;
            if (only != null) return null;
            only = w;
        }
        if (only != null && only.length() > 1 && (only.charAt(0) == '\'' || only.charAt(0) == '"') && only.endsWith(only.substring(0, 1)))
            only = only.substring(1, only.length() - 1);
        return only;
    }

    /** A short note near the mouse, that goes by itself: why a double click did nothing. */
    void hint(String text, double screenX, double screenY) {
        Tooltip tip = new Tooltip(text);
        tip.setAutoHide(true);
        tip.show(web, screenX + 12, screenY + 12);
        PauseTransition later = new PauseTransition(Duration.seconds(4));
        later.setOnFinished(e -> tip.hide());
        later.play();
    }

    /**
     * Types a command into the shell, as if the user had, and only if the shell is waiting for a command (see
     * {@link #shellReady}) and the user allows it: otherwise it writes nothing and returns false. It starts with
     * a space, so it stays out of the history if HISTCONTROL contains ignorespace (as on Ubuntu).
     */
    public boolean typeCommand(String command) {
        if (!typesCommands() || !shellReady()) return false;
        transport.send(" " + command + "\r");
        return true;
    }

    /**
     * Runs a command in the shell without it being seen, with the same conditions as {@link #typeCommand}. Only
     * for reading the shell's folder: its echo is discarded up to the signal. If the signal does not arrive
     * within 2 seconds everything is shown, so nothing is lost.
     */
    private boolean hiddenCommand(String command) {
        if (!typesCommands()) return false;
        if (!shellReady()) return false;
        hiding = true;
        hidden.setLength(0);
        transport.send(" printf '\\033]777;lemuri\\007'; " + command + "\r");
        PauseTransition wait = new PauseTransition(Duration.seconds(2));
        wait.setOnFinished(e -> {
            if (!hiding) return;
            hiding = false;
            String all = hidden.toString();
            hidden.setLength(0);
            write(all);
        });
        wait.play();
        return true;
    }

    /**
     * True if the cursor line is an empty shell prompt and no full-screen program is running:
     * writing there does not end up inside mysql, a REPL or the sudo password question, and does not
     * mix with what the user has already typed.
     */
    public boolean shellReady() {
        return ready && Boolean.TRUE.equals(web.getEngine().executeScript("shellReady()"));
    }

    /**
     * Asks the shell for its folder, with a hidden command that prints it in an
     * invisible sequence. If the shell is not at the prompt, or does not answer
     * within 3 seconds, null arrives.
     */
    public void currentFolder(Consumer<String> r) {
        onFolder = r;
        reply.setLength(0);
        if (!hiddenCommand("printf '\\033]777;lemuri-pwd;%s\\007' \"$PWD\"")) {
            onFolder = null;
            r.accept(null);
            return;
        }
        PauseTransition wait = new PauseTransition(Duration.seconds(3));
        wait.setOnFinished(e -> {
            if (onFolder != r) return;
            onFolder = null;
            String rest = reply.toString();
            reply.setLength(0);
            if (!rest.isEmpty()) write(rest);
            r.accept(null);
        });
        wait.play();
    }

    private void write(String d) {
        if (hiding) {
            hidden.append(d);
            int i = hidden.indexOf(MARK);
            if (i < 0) return;
            // The cursor is still on the prompt line, where the echo would have appeared: the shell's new prompt takes
            // its place, so the screen looks as before and the names of the last ls are still the last command's.
            d = "\r\u001b[K" + hidden.substring(i + MARK.length());
            hiding = false;
            hidden.setLength(0);
        }
        if (onFolder != null) {
            reply.append(d);
            int i = reply.indexOf(DIR);
            int j = i < 0 ? -1 : reply.indexOf("\u0007", i);
            if (i >= 0 && j < 0) return; // the reply is cut in half: wait for the rest
            d = reply.toString();
            reply.setLength(0);
            if (i >= 0) {
                String folder = d.substring(i + DIR.length(), j);
                d = d.substring(0, i) + d.substring(j + 1);
                Consumer<String> r = onFolder;
                onFolder = null;
                // Afterwards: the receiver may open a window, and meanwhile the terminal must finish writing.
                Platform.runLater(() -> r.accept(folder));
            }
        }
        if (!ready) {
            pending.append(d);
            return;
        }
        window.call("write", d);
    }

    /** The name as ls writes it, without the marks it adds. */
    /**
     * The file or folder name under the mouse in the output of an ls at the current prompt, cleaned as for the double click, and
     * the ls command that listed it: {name, command}. Null if the mouse is not on such a name.
     */
    public String[] nameForMenu() {
        if (!ready) return null;
        Object r = window.call("nameForMenu");
        if (!(r instanceof String s)) return null;
        int nl = s.indexOf('\n');
        String name = clear(nl < 0 ? s : s.substring(0, nl));
        if (name.isEmpty() || name.chars().anyMatch(Character::isISOControl)) return null;
        return new String[] {name, nl < 0 ? "" : s.substring(nl + 1)};
    }

    private static String clear(String name) {
        // xterm includes in the word the trailing ':' of "ls -R" and the '/' that "ls -F" adds to folders.
        String n = name.replaceAll("[:*@=|]+$", "");
        // ls quotes names with special characters: 'name' or "name".
        if (n.length() > 1 && (n.charAt(0) == '\'' || n.charAt(0) == '"') && n.charAt(n.length() - 1) == n.charAt(0)) n = n.substring(1, n.length() - 1);
        if (n.endsWith("/") && n.length() > 1) n = n.substring(0, n.length() - 1);
        return n;
    }

    public Transport transport() {
        return transport;
    }

    /** +1 for Ctrl+plus, -1 for Ctrl+minus, 0 for Ctrl+0; MIN_VALUE for any other key. */
    private static int zoom(KeyEvent e) {
        switch (e.getCode()) {
            case PLUS, ADD, EQUALS: return 1;
            case MINUS, SUBTRACT: return -1;
            case DIGIT0, NUMPAD0: return 0;
            default: break;
        }
        // Layouts where "+" has no key code of its own (on the Italian keyboard it is next to the è).
        return switch (e.getText()) {
            case "+" -> 1;
            case "-" -> -1;
            default -> Integer.MIN_VALUE;
        };
    }

    /** Changes the font of every open terminal and remembers it. */
    public static void setFontSize(int size) {
        int s = Math.max(MIN_FONT, Math.min(MAX_FONT, size));
        if (s == fontSize) return;
        fontSize = s;
        PREF.putInt(FONT_SIZE, s);
        for (Terminal t : List.copyOf(ALL)) if (t.ready) t.window.call("setFontSize", s);
    }

    /** Like {@link #focus} for a terminal that has moved but does not get the keyboard: one part of a split tab. */
    public void redraw() {
        Platform.runLater(() -> {
            if (ready) window.call("redraw");
        });
    }

    /** The theme of the window, for the scroll bar: light or dark like the one of the host tree. */
    private static boolean darkTheme;

    /** The theme changed (or was read at startup): every terminal draws its scroll bar in it. */
    public static void theme(boolean dark) {
        darkTheme = dark;
        for (Terminal t : List.copyOf(ALL)) if (t.ready) t.window.call("setLight", !dark);
    }

    /** Ghost mode: transparent page and terminal background, so the see-through window behind shows. */
    public void ghost(boolean on) {
        if (on == ghost.get()) return;
        ghost.set(on);
        web.setPageFill(on ? Color.TRANSPARENT : Color.web("#0d1117"));
        if (ready) window.call("setGhost", on);
    }

    /** {@link #ghost}, to follow. */
    public ReadOnlyBooleanProperty ghostShown() {
        return ghost;
    }

    /** Width of the scroll bar on the right of the page (terminal.html): 12 px, 7 in ghost mode. */
    public double scrollBarWidth() {
        return ghost.get() ? 7 : 12;
    }

    /** True while the keyboard types into this terminal. */
    public ObservableBooleanValue typing() {
        return web.focusedProperty();
    }

    /**
     * Gives focus to the terminal. It is often called right after the node was moved
     * to another view, when it is not in the scene yet: then it waits for it to get there.
     */
    public void focus() {
        if (getScene() == null) {
            sceneProperty().addListener(new ChangeListener<>() {
                @Override
                public void changed(ObservableValue<? extends Scene> o, Scene before, Scene now) {
                    if (now == null) return;
                    sceneProperty().removeListener(this);
                    focus();
                }
            });
            return;
        }
        // After layout: a node just added does not take focus until it has been laid out.
        Platform.runLater(() -> {
            web.requestFocus();
            if (ready) window.call("show");
        });
    }

    /** What is selected in the terminal, empty if nothing. */
    public String selection() {
        if (!ready) return "";
        Object s = window.call("selection");
        return s instanceof String t ? t : "";
    }

    /** Copies the selection to the clipboard; false if nothing is selected. */
    public boolean copy() {
        String s = selection();
        if (s.isEmpty()) return false;
        ClipboardContent c = new ClipboardContent();
        c.putString(s);
        Clipboard.getSystemClipboard().setContent(c);
        return true;
    }

    /**
     * Pastes as xterm does: in brackets when the shell asks for it (lines pasted into bash are not run one by
     * one), along the same path as typed input, so a synced tab gets it in every part. The text goes through
     * {@link #pastable} first: a web page can put in the clipboard more than it shows.
     */
    public void paste(String text) {
        String t = pastable(text);
        if (ready && !t.isEmpty()) window.call("paste", t);
    }

    /**
     * The text without the control characters but tab and line breaks, C1 included (0x9B is a CSI in 8 bits):
     * an ESC[201~ inside it would close the brackets early, and bash would run at once what follows
     * ("pastejacking"). As xterm with allowPasteControls off, kitty and Alacritty do.
     */
    static String pastable(String text) {
        if (text == null) return "";
        StringBuilder b = new StringBuilder(text.length());
        text.codePoints().forEach(c -> {
            if (c == '\t' || c == '\n' || c == '\r' || !Character.isISOControl(c)) b.appendCodePoint(c);
        });
        return b.toString();
    }

    public void pasteClipboard() {
        paste(clipboardText());
    }

    /** The text in the clipboard, or null. */
    public static String clipboardText() {
        Clipboard c = Clipboard.getSystemClipboard();
        return c.hasString() ? c.getString() : null;
    }

    public void close() {
        ALL.remove(this);
        transport.close();
    }

    /** Called from JavaScript: the methods must be public. */
    public class Bridge {
        public void ready(int columns, int lines) {
            ready = true;
            // Ghost mode was asked for before the page was ready.
            if (ghost.get()) window.call("setGhost", true);
            window.call("setLight", !darkTheme);
            // Ready while it is already the one in view: the focus requested earlier had not reached xterm.
            if (web.isFocused()) window.call("focus");
            if (!pending.isEmpty()) {
                window.call("write", pending.toString());
                pending.setLength(0);
            }
            transport.launch(columns, lines);
        }

        /**
         * Double click on a name listed by the last command ({@code command}, the ls that listed it), at
         * {@code x}, {@code y} in the page: see {@link DoubleClick}.
         */
        public void doubleClick(String name, String command, int x, int y) {
            Point2D at = web.localToScreen(x, y);
            DoubleClick.onName(Terminal.this, clear(name), command, at != null ? at.getX() : 0, at != null ? at.getY() : 0);
        }

        /** Double click on the empty space at the prompt: the shell lists its folder. */
        public void listHere() {
            if (typesCommands()) typeCommand("ls");
        }

        /** Text selected with the mouse: it becomes the primary selection, as in every Linux terminal. */
        public void selected(String text) {
            if (text != null && !text.isEmpty()) PrimarySelection.set(text);
        }

        /** A paste that reached the page by itself (the browser's own paste event): through the same filter. */
        public void paste(String text) {
            Terminal.this.paste(text);
        }

        public void input(String data) {
            input.accept(data);
        }

        public void resize(int columns, int lines) {
            transport.resize(columns, lines);
        }
    }
}
