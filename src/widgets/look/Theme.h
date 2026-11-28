#pragma once

#include <QColor>
#include <QList>
#include <QString>

/**
 * Every colour of the interface, by what it is for. Style sheets name them as @name and go through css(); code uses
 * them directly. The values here are the first theme's (Lemuri, dark); apply() puts another theme's in their place,
 * once, at startup, before any window is made (a change takes effect at the next start). The splash screen's drawing
 * keeps its own.
 */
namespace Theme {
/** The themes, in the order the settings list them: an id (saved) and the key of its name. */
struct Choice {
    const char *id, *name;
};
const QList<Choice> &choices();
/** Puts the colours of the theme `id` in place (an unknown id: the first theme). Before any window. */
void apply(const QString &id);
/** The theme in use, and whether it is a light one. */
QString current();
bool light();
/** The theme of the next start, kept beside the splash screen's choice (startup.json), read before the backend runs. */
QString saved();
void remember(const QString &id);

// The backdrop of the main window and the dialogs (see Backdrop): a diagonal shade from near black (top left) to a
// dark slate green (bottom right), and the waves across it, lit along their crests.
inline QColor backdropFrom("#0c0e0d");
inline QColor backdropMiddle("#151a18");
inline QColor backdropTo("#22302b");
inline QColor backdropWave("#4a7a6a");
inline QColor backdropCrest("#8fc9b5");
// See-through surfaces over the backdrop: the panels, their heads, the fields and tables inside them.
inline QColor veil(255, 255, 255, 6);
inline QColor veilHead(255, 255, 255, 10);
inline QColor veilDeep(0, 0, 0, 60);
/** The empty part of a meter (gauge ring, usage bar): light enough to show over the backdrop. */
inline QColor track(255, 255, 255, 22);
/** Under the mouse, over a control or the window: a light film on a dark theme, a dark one on a light theme. */
inline QColor hover(255, 255, 255, 36);
inline QColor hoverSoft(255, 255, 255, 26);
// Surfaces and lines
inline QColor bg("#1e1e1e");
inline QColor surface("#1e1e1e");
inline QColor headPanel("#252526");
inline QColor side("#252526");
inline QColor control("#262626");
// The edge of an empty check box or radio button, and under the mouse.
inline QColor checkEdge("#7a7a7a");
inline QColor checkEdgeHover("#a8a8a8");
inline QColor controlHover("#303030");
inline QColor border("#2d2d2d");
inline QColor edge("#3c3c3c");
inline QColor on("#2a2d2e");
inline QColor gap("#333333");
inline QColor black("#000000");
inline QColor tip("#2c2c2c");
inline QColor tipText("#d0d0d0");
// Text
inline QColor text("#e6e6e6");
inline QColor faint("#a0a0a0");
inline QColor disabled("#6e6e6e");
inline QColor topText("#e0e0e0");
inline QColor topFaint("#cccccc");
// The accent and selections
inline QColor accent("#3892d4");
inline QColor accentDark("#2a78b3");
inline QColor accentOff("#2a4f6b");
inline QColor accentOffText("#a9b9c6");
inline QColor textSelection("#264f78");
inline QColor rowSelected("#37373d");
inline QColor menuSelected("#3a3f46");
inline QColor listSelected("#1f4a6e");
inline QColor badge("#2f6fdf");
// Activity bar, tree, status bar
inline QColor activityBg("#333333");
inline QColor activityIcon("#8b8b8b");
inline QColor activityActive("#ffffff");
inline QColor statusBlue("#007acc");
inline QColor treeOn("#2a2d31");
inline QColor treeChoice("#2b3445");
inline QColor treeFocusChoice("#2f3d57");
inline QColor chevron("#8a8a8a");
inline QColor dropTarget("#4c8dff");
inline QColor dropText("#a9c7ff");
inline QColor sessionsIcon("#4fc1ff");
inline QColor teamIcon("#c586c0");
inline QColor updateIcon("#89d185");
inline QColor vaultIcon("#e2c08d");
// States
inline QColor ok("#21bf4b");
inline QColor idle("#9a9a9a");
inline QColor critical("#e3323f");
/** The text of a button that deletes something: lighter than `critical`, readable on the controls (about 5.5:1). */
inline QColor dangerText("#ff6b73");
/** The filled button of a destructive choice: white text on it reads well (about 6:1). */
inline QColor criticalFill("#b8262f");
inline QColor criticalFillHover("#9f1f28");
inline QColor danger("#d9776f");
inline QColor warn("#fcc45e");
inline QColor amber("#fbab19");
inline QColor failed("#f48771");
inline QColor failure("#f85149");
inline QColor errorSoft("#f0a8a8");
inline QColor okBack("#0c431a");
inline QColor okText("#7ad993");
inline QColor warnBack("#583c09");
inline QColor errorBack("#501216");
inline QColor errorText("#ed7982");
inline QColor infoBack("#163a55");
inline QColor infoText("#91c3e7");
// Terminal, splits, ghost mode
/**
 * The 16 ANSI colours of the terminal: the Tango palette, as xterm.js has it. Bold text takes the
 * bright ones (8-15), so a bold blue (directories in ls, the folder in the prompt) is a readable light blue.
 */
inline QColor terminalPalette[16] = {QColor("#2e3436"), QColor("#cc0000"), QColor("#4e9a06"), QColor("#c4a000"),
                                           QColor("#3465a4"), QColor("#75507b"), QColor("#06989a"), QColor("#d3d7cf"),
                                           QColor("#555753"), QColor("#ef2929"), QColor("#8ae234"), QColor("#fce94f"),
                                           QColor("#729fcf"), QColor("#ad7fa8"), QColor("#34e2e2"), QColor("#eeeeec")};
inline QColor terminalActions("#101317");
inline QColor terminalBg("#000000");
inline QColor terminalFg("#d6dde6");
inline QColor terminalSelection("#264f78");
inline QColor actionIcon("#8b949e");
inline QColor actionHover("#e6edf3");
inline QColor sync("#facc15");
inline QColor syncedBorder("#e2a33d");
inline QColor ghostControl("#b0b0b0");
inline QColor ghostAccent("#7cc4ff");
// Scroll bars
inline QColor handle("#2a2a2c");
inline QColor handleTop("#303032");
inline QColor handleBottom("#242426");
inline QColor handleLitTop("#3a3a3c");
inline QColor handleLitBottom("#2c2c2e");
inline QColor handleEdge("#404041");
inline QColor handleEdgeLit("#5a5a5c");

inline constexpr int RADIUS = 5;

/** A style sheet with its @names replaced by the colours above. */
QString css(QString sheet);
}  // namespace Theme
