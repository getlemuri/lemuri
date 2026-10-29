package lemuri.be.util;

import static org.junit.jupiter.api.Assertions.assertEquals;
import static org.junit.jupiter.api.Assertions.assertNotEquals;
import static org.junit.jupiter.api.Assertions.assertNull;

import java.util.List;
import org.junit.jupiter.api.Test;
import lemuri.be.dto.TrustedPeer;

/** Contact names are what deliveries show as "from": two must never look alike. */
class ContactNamesTest {
    @Test
    void cleanRemovesInvisibleAndDirectionCharacters() {
        assertEquals("bruno", ContactNames.clean("bru​no"));
        assertEquals("bruno", ContactNames.clean("‮bruno⁦"));
        assertEquals("bruno rossi", ContactNames.clean("  bruno \t\n rossi  "));
        // Compatibility forms become the plain letters (full-width, ligatures).
        assertEquals("bruno", ContactNames.clean("ｂｒｕｎｏ"));
        assertEquals("", ContactNames.clean(null));
        assertEquals(ContactNames.MAX, ContactNames.clean("x".repeat(200)).length());
    }

    @Test
    void lookAlikesHaveTheSameSkeleton() {
        String bruno = ContactNames.skeleton("bruno");
        for (String other : List.of("Bruno", "BRUNO", "brunо", "вruno", "brunó", "bru no", "brun0"))
            assertEquals(bruno, ContactNames.skeleton(other), other);
        assertEquals(ContactNames.skeleton("mario"), ContactNames.skeleton("rnario"));
        assertEquals(ContactNames.skeleton("ilaria"), ContactNames.skeleton("Ilaria"));
        assertNotEquals(bruno, ContactNames.skeleton("bruna"));
    }

    @Test
    void takenByIgnoresTheSameKey() {
        List<TrustedPeer> contacts = List.of(new TrustedPeer("K1", "bruno"));
        assertEquals("bruno", ContactNames.takenBy(contacts, "brunо", "K2"));
        assertNull(ContactNames.takenBy(contacts, "bruno", "K1"));
        assertNull(ContactNames.takenBy(contacts, "carla", "K2"));
    }

    /** Analysis #11: look-alikes no table lists are refused because they mix scripts or are phonetic letters. */
    @Test
    void mixedScriptsAndPhoneticLettersAreRefused() {
        assertEquals("contact.nameMixedScripts", ContactNames.problem("mari\u0585"));   // Armenian o
        assertEquals("contact.nameMixedScripts", ContactNames.problem("brun\u043e"));   // Cyrillic o
        assertEquals("contact.nameOddLetters", ContactNames.problem("m\u0251rio"));     // Latin alpha
        assertEquals("contact.nameEmpty", ContactNames.problem(" \u200B "));
        assertEquals("contact.nameMixedScripts", ContactNames.problem("mar\u04C0o"));  // Cyrillic palochka, like an I or l
        // The small commercial at becomes a plain @ (NFKC): the same name as an existing contact, so taken.
        assertEquals("anna@pc", ContactNames.takenBy(List.of(new TrustedPeer("K1", "anna@pc")), "anna\uFE6Bpc", "K2"));
        for (String ok : List.of("bruno", "Bruno Rossi", "anna@pc-01", "\u041c\u0430\u0440\u0438\u044f", "\u5c71\u7530\u30bf\u30ed\u30a6", "\uae40\ubbfc\uc218"))
            assertEquals(null, ContactNames.problem(ok), ok);
    }
}
