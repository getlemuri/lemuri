package lemuri.be.terminal;

import static org.junit.jupiter.api.Assertions.assertEquals;

import java.nio.charset.StandardCharsets;
import java.util.Arrays;
import org.junit.jupiter.api.Test;

class Utf8Test {
    /** Decodes the text splitting it into reads of {@code step} bytes. */
    private static String inPieces(String text, int step) {
        byte[] b = text.getBytes(StandardCharsets.UTF_8);
        SshTransport.Utf8 u = new SshTransport.Utf8();
        StringBuilder out = new StringBuilder();
        for (int i = 0; i < b.length; i += step) {
            byte[] piece = Arrays.copyOfRange(b, i, Math.min(b.length, i + step));
            out.append(u.decode(piece, piece.length));
        }
        return out.toString();
    }

    @Test
    void splitCharactersAreReassembled() {
        String text = "àèìòù € 你好 🙂 ok";
        for (int step = 1; step <= 5; step++) assertEquals(text, inPieces(text, step), "passo " + step);
    }

    @Test
    void anIncompleteCharacterWaitsForTheRest() {
        byte[] euro = "€".getBytes(StandardCharsets.UTF_8);
        SshTransport.Utf8 u = new SshTransport.Utf8();
        assertEquals("", u.decode(Arrays.copyOf(euro, 2), 2));
        assertEquals("€", u.decode(new byte[] {euro[2]}, 1));
    }
}
