#include "sharing/ContactNames.h"

#include <QHash>
#include <QSet>

namespace {
/** Letters of other scripts that look like a Latin one, after lower case. */
const QHash<char32_t, char> LOOKALIKE{
    // Cyrillic
    {0x0430, 'a'}, {0x0432, 'b'}, {0x0435, 'e'}, {0x043e, 'o'}, {0x0440, 'p'}, {0x0441, 'c'}, {0x0443, 'y'}, {0x0445, 'x'},
    {0x0456, 'l'}, {0x0458, 'j'}, {0x0455, 's'}, {0x0501, 'd'}, {0x04bb, 'h'}, {0x04cf, 'l'}, {0x051b, 'q'}, {0x051d, 'w'},
    {0x043a, 'k'}, {0x043c, 'm'}, {0x043d, 'h'}, {0x0442, 't'},
    // Greek
    {0x03bf, 'o'}, {0x03b1, 'a'}, {0x03bd, 'v'}, {0x03c1, 'p'}, {0x03b9, 'l'}, {0x03ba, 'k'}, {0x03c5, 'u'}, {0x03c7, 'x'},
    {0x03b5, 'e'}, {0x03c4, 't'},
    // Latin look-alikes and digits
    {0x0261, 'g'}, {0x0131, 'l'}, {'i', 'l'}, {'1', 'l'}, {'|', 'l'}, {'0', 'o'}};

bool dropped(QChar::Category c) {
    return c == QChar::Other_Format || c == QChar::Other_PrivateUse || c == QChar::Other_Surrogate || c == QChar::Other_NotAssigned;
}

QString fromUcs4(const QList<uint> &l) {
    return QString::fromUcs4(reinterpret_cast<const char32_t *>(l.constData()), l.size());
}
}  // namespace

bool ContactNames::unicodeSpace(char32_t c) {
    if (c == 0x00a0 || c == 0x2007 || c == 0x202f) return false;
    if ((c >= 0x09 && c <= 0x0d) || (c >= 0x1c && c <= 0x1f)) return true;
    QChar::Category k = QChar::category(c);
    return k == QChar::Separator_Space || k == QChar::Separator_Line || k == QChar::Separator_Paragraph;
}

QString ContactNames::strip(const QString &s) {
    QList<uint> l = s.toUcs4();
    qsizetype a = 0, z = l.size();
    while (a < z && unicodeSpace(l[a])) a++;
    while (z > a && unicodeSpace(l[z - 1])) z--;
    return fromUcs4(l.mid(a, z - a));
}

QString ContactNames::clean(const QString &name) {
    QList<uint> b;
    for (char32_t c : name.normalized(QString::NormalizationForm_KC).toUcs4()) {
        QChar::Category k = QChar::category(c);
        if (dropped(k)) continue;
        bool space = k == QChar::Separator_Space || k == QChar::Separator_Line || k == QChar::Separator_Paragraph;
        if (k == QChar::Other_Control || unicodeSpace(c) || space) {
            // Runs of spaces become one.
            if (!b.isEmpty() && b.last() == ' ') continue;
            b.append(' ');
        } else {
            b.append(c);
        }
    }
    QList<uint> n = strip(fromUcs4(b)).toUcs4();
    if (n.size() > MAX) n.resize(MAX);
    return fromUcs4(n);
}

QString ContactNames::skeleton(const QString &name) {
    QString b;
    for (char32_t c : clean(name).toLower().normalized(QString::NormalizationForm_D).toUcs4()) {
        QChar::Category k = QChar::category(c);
        if (k == QChar::Mark_NonSpacing || k == QChar::Mark_Enclosing || k == QChar::Mark_SpacingCombining || c == ' ') continue;
        auto latin = LOOKALIKE.constFind(c);
        if (latin != LOOKALIKE.constEnd()) b += QChar(latin.value());
        else b += QString::fromUcs4(&c, 1);
    }
    return b.replace("rn", "m").replace("vv", "w");
}

QString ContactNames::problem(const QString &name) {
    QString n = clean(name);
    if (n.isEmpty()) return "contact.nameEmpty";
    QSet<int> scripts;
    for (char32_t c : n.toUcs4()) {
        // IPA extensions, phonetic extensions and their supplement: Latin look-alikes the script check cannot see.
        if ((c >= 0x0250 && c <= 0x02af) || (c >= 0x1d00 && c <= 0x1dbf)) return "contact.nameOddLetters";
        if (!QChar::isLetter(c)) continue;
        QChar::Script s = QChar::script(c);
        switch (s) {
        case QChar::Script_Common:
        case QChar::Script_Inherited:
            break;
        case QChar::Script_Han:
        case QChar::Script_Hiragana:
        case QChar::Script_Katakana:
        case QChar::Script_Hangul:
        case QChar::Script_Bopomofo:
            scripts.insert(-1);  // Chinese, Japanese and Korean count as one
            break;
        default:
            scripts.insert(int(s));
        }
    }
    return scripts.size() > 1 ? "contact.nameMixedScripts" : QString();
}

QString ContactNames::takenBy(const QList<TrustedPeer> &contacts, const QString &name, const QString &key) {
    QString s = skeleton(name);
    for (const TrustedPeer &t : contacts)
        if (t.key != key && skeleton(t.name) == s) return t.name;
    return QString();
}
