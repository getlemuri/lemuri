package lemuri.fe.v.terminal;

import java.awt.Toolkit;
import java.awt.datatransfer.Clipboard;
import java.awt.datatransfer.DataFlavor;
import java.awt.datatransfer.StringSelection;
import java.awt.datatransfer.UnsupportedFlavorException;
import java.io.IOException;
import java.util.function.Consumer;
import javafx.application.Platform;

/**
 * The X11 primary selection, as every Linux terminal uses it: what is selected can be pasted with the middle
 * button, here and in the other apps. JavaFX does not reach it, AWT does; where there is none (macOS,
 * a headless test) it is the normal clipboard. AWT is called off the JavaFX thread: the X server answers when
 * the app that owns the selection does.
 */
final class PrimarySelection {
    private PrimarySelection() {}

    private static Clipboard primary() {
        try {
            return Toolkit.getDefaultToolkit().getSystemSelection();
        } catch (UnsupportedOperationException e) {
            // HeadlessException included: no display, no selection.
            return null;
        }
    }

    static void set(String text) {
        Thread t = new Thread(() -> {
            Clipboard p = primary();
            try {
                if (p != null) p.setContents(new StringSelection(text), null);
            } catch (IllegalStateException e) {
                // Busy for a moment (another app is reading it): this selection is not offered, the next one will be.
            }
        }, "primary-selection");
        t.setDaemon(true);
        t.start();
    }

    /** The primary selection, or null if there is none; {@code then} runs on the JavaFX thread. */
    static void get(Consumer<String> then) {
        Thread t = new Thread(() -> {
            String text = null;
            Clipboard p = primary();
            try {
                if (p != null && p.isDataFlavorAvailable(DataFlavor.stringFlavor)) text = (String) p.getData(DataFlavor.stringFlavor);
            } catch (IllegalStateException | UnsupportedFlavorException | IOException e) {
                // Nothing usable in it: nothing is pasted.
            }
            String found = text;
            Platform.runLater(() -> then.accept(found));
        }, "primary-selection");
        t.setDaemon(true);
        t.start();
    }
}
