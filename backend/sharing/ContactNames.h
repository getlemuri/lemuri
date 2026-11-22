#pragma once

#include <QList>
#include <QString>

/** A PC paired with the code, or a key card added: its key and the name it was saved with. */
struct TrustedPeer {
    QString key, name;
    bool operator==(const TrustedPeer &o) const = default;
};

/**
 * Contact names. The name is what a delivery shows as "from", so it is the one place
 * where two contacts must never look alike: every way a contact is saved goes through here.
 */
namespace ContactNames {
constexpr int MAX = 64;
/**
 * The name as saved and shown: compatibility forms folded (NFKC), invisible and format characters removed, any run
 * of spaces or line breaks as one space, at most MAX characters.
 */
QString clean(const QString &name);
/** What the name looks like, roughly: no case, accents or spaces, other scripts' look-alikes as Latin letters. */
QString skeleton(const QString &name);
/** Null if `name` can be a contact's name; otherwise the i18n key of the reason. */
QString problem(const QString &name);
/** The contact, with another key, whose name looks like `name`; null if there is none. */
QString takenBy(const QList<TrustedPeer> &contacts, const QString &name, const QString &key);
/** The text without white space (unicodeSpace) at both ends. */
QString strip(const QString &s);
/** White space: Unicode space separators (but not the no-break ones), line and paragraph separators, and the ASCII
 * controls \t \n \v \f \r and 0x1c-0x1f. The first Lemuri trims names with the same set. */
bool unicodeSpace(char32_t c);
}  // namespace ContactNames
