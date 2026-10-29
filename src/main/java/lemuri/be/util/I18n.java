package lemuri.be.util;

import java.text.MessageFormat;
import java.util.List;
import java.util.Locale;
import java.util.MissingResourceException;
import java.util.ResourceBundle;

/**
 * The interface texts, in lemuri/i18n/messages*.properties: English as the base,
 * plus the translations (messages_it). Every text goes through MessageFormat, so values
 * go in {0}, {1}… and an apostrophe in the file is written twice: can''t.
 */
public final class I18n {
    private I18n() {}

    private static final String BASE = "lemuri.i18n.messages";
    /** The languages that have a translation; the others use English. */
    public static final List<String> LANGUAGES = List.of("en", "it");
    private static volatile ResourceBundle texts = load(Locale.getDefault());

    /** Call once at startup, with the chosen language. */
    public static void language(Locale l) {
        texts = load(l);
    }

    private static ResourceBundle load(Locale l) {
        // No fallback to the system language: a language without a translation goes to the base file (English).
        return ResourceBundle.getBundle(BASE, l, ResourceBundle.Control.getNoFallbackControl(ResourceBundle.Control.FORMAT_PROPERTIES));
    }

    /**
     * A message received from another PC: if it is an i18n key it is translated into our language,
     * otherwise (an old version sending free text) it is shown as it is.
     */
    public static String ifKey(String message) {
        return message != null && texts.containsKey(message) ? t(message) : message;
    }

    /** The text of the key with the values in place. A missing key shows as !key! instead of breaking the app. */
    public static String t(String key, Object... values) {
        return formatValue(texts, key, values);
    }

    /** Like {@link #t}, but always in English: for the technical log, which ends up in bug reports. */
    public static String english(String key, Object... values) {
        return formatValue(ENGLISH, key, values);
    }

    private static final ResourceBundle ENGLISH = load(Locale.ROOT);

    private static String formatValue(ResourceBundle b, String key, Object... values) {
        String s;
        try {
            s = b.getString(key);
        } catch (MissingResourceException e) {
            return "!" + key + "!";
        }
        // Numbers as text: MessageFormat would write port 47821 as "47,821".
        Object[] v = new Object[values.length];
        for (int i = 0; i < values.length; i++) v[i] = values[i] instanceof Number ? String.valueOf(values[i]) : values[i];
        return new MessageFormat(s, b.getLocale()).format(v);
    }
}
