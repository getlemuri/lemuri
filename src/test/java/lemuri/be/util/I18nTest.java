package lemuri.be.util;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertFalse;
import static org.junit.jupiter.api.Assertions.assertTrue;

import java.io.Reader;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.text.MessageFormat;
import java.util.Locale;
import java.util.Properties;
import java.util.Set;
import java.util.TreeSet;
import java.util.regex.Matcher;
import java.util.regex.Pattern;
import java.util.stream.Stream;
import org.junit.jupiter.api.Test;

/** The interface texts: two aligned languages, valid texts, no missing or forgotten keys. */
class I18nTest {
    private static final Path TEXTS = Path.of("src/main/resources/lemuri/i18n");

    private static Properties read(String name) throws Exception {
        Properties p = new Properties();
        try (Reader r = Files.newBufferedReader(TEXTS.resolve(name), StandardCharsets.UTF_8)) {
            p.load(r);
        }
        return p;
    }

    /** The keys used in the code: I18n.t("key", …). */
    private static Set<String> usedOnes() throws Exception {
        Set<String> keys = new TreeSet<>();
        Pattern usage = Pattern.compile("I18n\\.t\\(\\s*\"([^\"]+)\"");
        try (Stream<Path> file = Files.walk(Path.of("src/main/java"))) {
            for (Path f : file.filter(x -> x.toString().endsWith(".java")).toList()) {
                Matcher m = usage.matcher(Files.readString(f));
                while (m.find()) keys.add(m.group(1));
            }
        }
        return keys;
    }

    private static Set<String> placeholders(String text) {
        Set<String> s = new TreeSet<>();
        Matcher m = Pattern.compile("\\{(\\d+)").matcher(text);
        while (m.find()) s.add(m.group(1));
        return s;
    }

    @Test
    void bothLanguagesHaveTheSameKeysAndPlaceholders() throws Exception {
        Properties en = read("messages.properties"), it = read("messages_it.properties");
        assertEquals(new TreeSet<>(en.stringPropertyNames()), new TreeSet<>(it.stringPropertyNames()));
        for (String k : en.stringPropertyNames())
            assertEquals(placeholders(en.getProperty(k)), placeholders(it.getProperty(k)), "placeholders of " + k);
    }

    @Test
    void everyTextIsValidForMessageFormat() throws Exception {
        for (String name : new String[] {"messages.properties", "messages_it.properties"}) {
            Properties p = read(name);
            for (String k : p.stringPropertyNames()) {
                String v = p.getProperty(k);
                // A single apostrophe opens a quotation for MessageFormat and makes the rest of the text disappear.
                assertFalse(Pattern.compile("(?<!')'(?!')").matcher(v).find(), name + " " + k + ": single apostrophe in \"" + v + "\"");
                new MessageFormat(v);
            }
        }
    }

    /** All strings in the code: a key written like this (for example the reasons travelling over the LAN) counts as used. */
    private static Set<String> strings() throws Exception {
        Set<String> all = new TreeSet<>();
        Pattern literal = Pattern.compile("\"([a-z][a-zA-Z]*(?:\\.[a-zA-Z]+)+)\"");
        try (Stream<Path> file = Files.walk(Path.of("src/main/java"))) {
            for (Path f : file.filter(x -> x.toString().endsWith(".java")).toList()) {
                Matcher m = literal.matcher(Files.readString(f));
                while (m.find()) all.add(m.group(1));
            }
        }
        return all;
    }

    @Test
    void everyUsedKeyExistsAndEveryKeyIsUsed() throws Exception {
        Set<String> inFile = new TreeSet<>(read("messages.properties").stringPropertyNames()), inCode = usedOnes();
        Set<String> missing = new TreeSet<>(inCode);
        missing.removeAll(inFile);
        assertTrue(missing.isEmpty(), "keys used in the code but missing: " + missing);
        Set<String> useless = new TreeSet<>(inFile);
        useless.removeAll(inCode);
        useless.removeAll(strings());
        assertTrue(useless.isEmpty(), "keys never used: " + useless);
    }

    @Test
    void noApostropheLost() throws Exception {
        // "nellarea", "cant": an apostrophe lost on the way. "nella", "alla", "questo" instead are real Italian words.
        Pattern italian = Pattern.compile("\\b(nell|dell|all|dall|sull|quest)(?![aeio]\\b)[aeiouàèéìòù]\\w*", Pattern.CASE_INSENSITIVE);
        Pattern english = Pattern.compile("\\b(cant|dont|doesnt|isnt|wont|hasnt|arent|didnt|couldnt|youre|theyre)\\b", Pattern.CASE_INSENSITIVE);
        for (String name : new String[] {"messages.properties", "messages_it.properties"}) {
            Pattern lost = name.contains("_it") ? italian : english;
            Properties p = read(name);
            for (String k : p.stringPropertyNames()) {
                String v = p.getProperty(k).replace("''", "'");
                Matcher m = lost.matcher(v);
                assertFalse(m.find(), name + " " + k + ": missing apostrophe? \"" + v + "\"");
            }
        }
    }

    @Test
    void numbersGetNoThousandsSeparator() {
        try {
            I18n.language(Locale.ITALIAN);
            assertEquals("In ascolto sulla rete locale (TCP 47821, UDP 47820) come a@b.", I18n.t("lan.msg.on", 47821, 47820, "a@b"));
            I18n.language(Locale.ENGLISH);
            assertEquals("Listening on the local network (TCP 47821, UDP 47820) as a@b.", I18n.t("lan.msg.on", 47821, 47820, "a@b"));
            // A key that does not exist shows as !key!, without exceptions.
            assertEquals("!non.esiste!", I18n.t("non.esiste"));
        } finally {
            I18n.language(Locale.getDefault());
        }
    }
}
