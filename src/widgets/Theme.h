#pragma once

#include <QColor>
#include <QHash>
#include <QRegularExpression>
#include <QString>

/**
 * Every colour of the interface (the dark theme), by what it is for. Style sheets
 * name them as @name and go through css(); code uses them directly. The splash screen's drawing keeps its own.
 */
namespace Theme {
// Surfaces and lines
inline const QColor bg("#1e1e1e");
inline const QColor surface("#1e1e1e");
inline const QColor headPanel("#252526");
inline const QColor side("#252526");
inline const QColor control("#262626");
// The edge of an empty check box or radio button, and under the mouse.
inline const QColor checkEdge("#7a7a7a");
inline const QColor checkEdgeHover("#a8a8a8");
inline const QColor controlHover("#303030");
inline const QColor border("#2d2d2d");
inline const QColor edge("#3c3c3c");
inline const QColor on("#2a2d2e");
inline const QColor gap("#333333");
inline const QColor black("#000000");
inline const QColor tip("#2c2c2c");
inline const QColor tipText("#d0d0d0");
// Text
inline const QColor text("#e6e6e6");
inline const QColor faint("#a0a0a0");
inline const QColor disabled("#6e6e6e");
inline const QColor topText("#e0e0e0");
inline const QColor topFaint("#cccccc");
// The accent and selections
inline const QColor accent("#3892d4");
inline const QColor accentDark("#2a78b3");
inline const QColor accentOff("#2a4f6b");
inline const QColor accentOffText("#a9b9c6");
inline const QColor textSelection("#264f78");
inline const QColor rowSelected("#37373d");
inline const QColor menuSelected("#3a3f46");
inline const QColor listSelected("#1f4a6e");
inline const QColor badge("#2f6fdf");
// Activity bar, tree, status bar
inline const QColor activityBg("#333333");
inline const QColor activityIcon("#8b8b8b");
inline const QColor activityActive("#ffffff");
inline const QColor statusBlue("#007acc");
inline const QColor treeOn("#2a2d31");
inline const QColor treeChoice("#2b3445");
inline const QColor treeFocusChoice("#2f3d57");
inline const QColor chevron("#8a8a8a");
inline const QColor dropTarget("#4c8dff");
inline const QColor dropText("#a9c7ff");
inline const QColor sessionsIcon("#4fc1ff");
inline const QColor teamIcon("#c586c0");
inline const QColor updateIcon("#89d185");
inline const QColor vaultIcon("#e2c08d");
// States
inline const QColor ok("#21bf4b");
inline const QColor idle("#9a9a9a");
inline const QColor critical("#e3323f");
inline const QColor danger("#d9776f");
inline const QColor warn("#fcc45e");
inline const QColor amber("#fbab19");
inline const QColor failed("#f48771");
inline const QColor failure("#f85149");
inline const QColor errorSoft("#f0a8a8");
inline const QColor okBack("#0c431a");
inline const QColor okText("#7ad993");
inline const QColor warnBack("#583c09");
inline const QColor errorBack("#501216");
inline const QColor errorText("#ed7982");
inline const QColor infoBack("#163a55");
inline const QColor infoText("#91c3e7");
// Terminal, splits, ghost mode
/**
 * The 16 ANSI colours of the terminal: the Tango palette, as xterm.js has it. Bold text takes the
 * bright ones (8-15), so a bold blue (directories in ls, the folder in the prompt) is a readable light blue.
 */
inline const QColor terminalPalette[16] = {QColor("#2e3436"), QColor("#cc0000"), QColor("#4e9a06"), QColor("#c4a000"),
                                           QColor("#3465a4"), QColor("#75507b"), QColor("#06989a"), QColor("#d3d7cf"),
                                           QColor("#555753"), QColor("#ef2929"), QColor("#8ae234"), QColor("#fce94f"),
                                           QColor("#729fcf"), QColor("#ad7fa8"), QColor("#34e2e2"), QColor("#eeeeec")};
inline const QColor terminalActions("#101317");
inline const QColor actionIcon("#8b949e");
inline const QColor actionHover("#e6edf3");
inline const QColor sync("#facc15");
inline const QColor syncedBorder("#e2a33d");
inline const QColor ghostControl("#b0b0b0");
inline const QColor ghostAccent("#7cc4ff");
// Scroll bars
inline const QColor scrollTrack("#2b2b2c");
inline const QColor scrollEdge("#525252");
inline const QColor handle("#2a2a2c");
inline const QColor handleTop("#303032");
inline const QColor handleBottom("#242426");
inline const QColor handleLitTop("#3a3a3c");
inline const QColor handleLitBottom("#2c2c2e");
inline const QColor handleEdge("#404041");
inline const QColor handleEdgeLit("#5a5a5c");

inline constexpr int RADIUS = 5;

/** A style sheet with its @names replaced by the colours above. */
inline QString css(QString sheet) {
    static const QHash<QString, QColor> named{
        {"bg", bg}, {"headPanel", headPanel}, {"control", control}, {"controlHover", controlHover}, {"border", border},
        {"edge", edge}, {"on", on}, {"black", black}, {"tip", tip}, {"tipText", tipText}, {"text", text}, {"faint", faint},
        {"disabled", disabled}, {"topText", topText}, {"topFaint", topFaint}, {"accent", accent}, {"accentDark", accentDark},
        {"accentOff", accentOff}, {"accentOffText", accentOffText}, {"textSelection", textSelection},
        {"rowSelected", rowSelected}, {"menuSelected", menuSelected}, {"listSelected", listSelected}, {"badge", badge},
        {"dropTarget", dropTarget}, {"dropText", dropText}, {"critical", critical}, {"warn", warn}, {"amber", amber},
        {"failed", failed}, {"failure", failure}, {"errorSoft", errorSoft}, {"okBack", okBack}, {"okText", okText},
        {"warnBack", warnBack}, {"errorBack", errorBack}, {"errorText", errorText}, {"infoBack", infoBack},
        {"infoText", infoText}, {"terminalActions", terminalActions}, {"actionIcon", actionIcon},
        {"actionHover", actionHover}, {"sync", sync}, {"syncedBorder", syncedBorder}, {"ghostControl", ghostControl},
        {"ghostAccent", ghostAccent}, {"scrollTrack", scrollTrack}, {"scrollEdge", scrollEdge}, {"handle", handle},
        {"handleTop", handleTop}, {"handleBottom", handleBottom}, {"handleLitTop", handleLitTop},
        {"handleLitBottom", handleLitBottom}, {"handleEdge", handleEdge}, {"handleEdgeLit", handleEdgeLit},
        {"sessionsIcon", sessionsIcon}, {"teamIcon", teamIcon}, {"updateIcon", updateIcon}, {"vaultIcon", vaultIcon}};
    static const QRegularExpression token("@([A-Za-z]+)");
    QString out;
    qsizetype last = 0;
    for (auto it = token.globalMatch(sheet); it.hasNext();) {
        QRegularExpressionMatch m = it.next();
        auto c = named.constFind(m.captured(1));
        out += sheet.mid(last, m.capturedStart() - last);
        out += c != named.constEnd() ? c->name() : m.captured(0);
        last = m.capturedEnd();
    }
    return out + sheet.mid(last);
}
}  // namespace Theme
