package lemuri.be.util;

import java.text.Normalizer;
import java.util.HashSet;
import java.util.List;
import java.util.Locale;
import java.util.Map;
import java.util.Set;
import lemuri.be.dto.TrustedPeer;

/**
 * Contact names. The name is what a delivery shows as "from", so it is the one place where two
 * contacts must never look alike: every way a contact is saved (key line, pairing) goes through here.
 */
public final class ContactNames {
    static final int MAX = 64;

    /** Letters of other scripts that look like a Latin one, after lower case. */
    private static final Map<Integer, Character> LOOKALIKE = Map.ofEntries(
            // Cyrillic
            Map.entry(0x0430, 'a'), Map.entry(0x0432, 'b'), Map.entry(0x0435, 'e'), Map.entry(0x043e, 'o'),
            Map.entry(0x0440, 'p'), Map.entry(0x0441, 'c'), Map.entry(0x0443, 'y'), Map.entry(0x0445, 'x'),
            Map.entry(0x0456, 'l'), Map.entry(0x0458, 'j'), Map.entry(0x0455, 's'), Map.entry(0x0501, 'd'),
            Map.entry(0x04bb, 'h'), Map.entry(0x04cf, 'l'), Map.entry(0x051b, 'q'), Map.entry(0x051d, 'w'),
            Map.entry(0x043a, 'k'), Map.entry(0x043c, 'm'), Map.entry(0x043d, 'h'), Map.entry(0x0442, 't'),
            // Greek
            Map.entry(0x03bf, 'o'), Map.entry(0x03b1, 'a'), Map.entry(0x03bd, 'v'), Map.entry(0x03c1, 'p'),
            Map.entry(0x03b9, 'l'), Map.entry(0x03ba, 'k'), Map.entry(0x03c5, 'u'), Map.entry(0x03c7, 'x'),
            Map.entry(0x03b5, 'e'), Map.entry(0x03c4, 't'),
            // Latin look-alikes and digits
            Map.entry(0x0261, 'g'), Map.entry(0x0131, 'l'), Map.entry((int) 'i', 'l'), Map.entry((int) '1', 'l'),
            Map.entry((int) '|', 'l'), Map.entry((int) '0', 'o'));

    private ContactNames() {}

    /**
     * The name as it is saved and shown: compatibility forms folded (NFKC), invisible and format characters
     * (zero-width spaces, text direction marks) removed, any run of spaces or line breaks as one space,
     * at most {@value #MAX} characters.
     */
    public static String clean(String name) {
        if (name == null) return "";
        StringBuilder b = new StringBuilder();
        Normalizer.normalize(name, Normalizer.Form.NFKC).codePoints().forEach(c -> {
            int type = Character.getType(c);
            if (type == Character.FORMAT || type == Character.PRIVATE_USE || type == Character.SURROGATE || type == Character.UNASSIGNED) return;
            if (type == Character.CONTROL || Character.isWhitespace(c) || Character.isSpaceChar(c)) b.append(' ');
            else b.appendCodePoint(c);
        });
        String n = b.toString().replaceAll(" {2,}", " ").strip();
        return n.codePointCount(0, n.length()) > MAX ? n.substring(0, n.offsetByCodePoints(0, MAX)) : n;
    }

    /** What the name looks like, roughly: without case, accents and spaces, other scripts' look-alikes as Latin letters. */
    static String skeleton(String name) {
        StringBuilder b = new StringBuilder();
        Normalizer.normalize(clean(name).toLowerCase(Locale.ROOT), Normalizer.Form.NFD).codePoints().forEach(c -> {
            int type = Character.getType(c);
            if (type == Character.NON_SPACING_MARK || type == Character.ENCLOSING_MARK || type == Character.COMBINING_SPACING_MARK) return;
            if (c == ' ') return;
            Character latin = LOOKALIKE.get(c);
            if (latin != null) b.append(latin.charValue());
            else b.appendCodePoint(c);
        });
        return b.toString().replace("rn", "m").replace("vv", "w");
    }

    /**
     * Null if {@code name} can be a contact's name; otherwise the i18n key of the reason. Refused: an empty
     * name, one that mixes scripts (Latin with Cyrillic, Greek, Armenian...: no look-alike table is ever
     * complete, so a mix is not allowed at all, like browsers do for domain names; Chinese, Japanese and
     * Korean count as one), and phonetic letters such as "ɑ", Latin look-alikes the script check cannot see.
     */
    public static String problem(String name) {
        String n = clean(name);
        if (n.isEmpty()) return "contact.nameEmpty";
        Set<String> scripts = new HashSet<>();
        for (int c : n.codePoints().toArray()) {
            Character.UnicodeBlock block = Character.UnicodeBlock.of(c);
            if (block == Character.UnicodeBlock.IPA_EXTENSIONS || block == Character.UnicodeBlock.PHONETIC_EXTENSIONS
                    || block == Character.UnicodeBlock.PHONETIC_EXTENSIONS_SUPPLEMENT) return "contact.nameOddLetters";
            if (!Character.isLetter(c)) continue;
            Character.UnicodeScript s = Character.UnicodeScript.of(c);
            switch (s) {
                case COMMON, INHERITED -> { }
                case HAN, HIRAGANA, KATAKANA, HANGUL, BOPOMOFO -> scripts.add("CJK");
                default -> scripts.add(s.name());
            }
        }
        return scripts.size() > 1 ? "contact.nameMixedScripts" : null;
    }

    /** The contact, with another key, whose name looks like {@code name}; null if there is none. */
    public static String takenBy(List<TrustedPeer> contacts, String name, String key) {
        String s = skeleton(name);
        for (TrustedPeer t : contacts)
            if (!t.key().equals(key) && skeleton(t.name()).equals(s)) return t.name();
        return null;
    }
}
