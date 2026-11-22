#pragma once

#include <QFont>
#include <QPainter>
#include <QWidget>
#include <deque>
#include <functional>
#include <vector>

extern "C" {
#include <vterm.h>
}

/**
 * A terminal drawn by Qt: libvterm keeps the screen (escape sequences, colours, the alternate screen of full-screen
 * programs), this widget draws it, sends keys and pastes as the bytes a terminal sends, and keeps the lines that
 * scroll off the top. What the shell prints comes in through write(); what the user types goes out by input().
 */
class TerminalView : public QWidget {
    Q_OBJECT
    friend class TerminalViewTest;
public:
    explicit TerminalView(QWidget *parent = nullptr);
    /**
     * Whether Lemuri may type commands into this shell (the "terminalCommands" setting, given by the Model). Off unless
     * the user turns it on: what looks like a prompt may be a program reading its input, which would get the command.
     */
    void setTypesCommands(bool on) { typesCommands = on; }
    bool typesCommandsOn() const { return typesCommands; }
    /** Ghost mode: no background of its own, the window behind (black and see-through) gives it. */
    void setGhost(bool on);
    ~TerminalView() override;

    void write(const QString &text);
    int columns() const { return cols; }
    int lines() const { return rows; }
    bool hasSelection() const { return !(selStart == selEnd); }
    void copySelection() { copy(true); }
    void pasteClipboard();
    /**
     * The folder the shell is in, asked with a command the user does not see;
     * `then` gets it, or an empty string if the shell is busy or does not answer within 3 seconds.
     */
    void currentFolder(std::function<void(const QString &)> then);
    /** Types a command as if by hand, only if Lemuri may and the shell waits at an empty prompt. */
    bool typeCommand(const QString &command);
    /** A short note near the mouse, that goes by itself: why a double click did nothing. */
    void hint(const QString &text, const QPoint &global);
    /** The name under the last right click, from an ls at the current prompt, and that ls; empty if none. */
    /** The middle of the last place where `text` is on screen, in the view's coordinates; (-1, -1) if nowhere (pictures). */
    QPoint whereIs(const QString &text) const;
    QString menuName() const { return menuHit.name; }
    QString menuCommand() const { return menuHit.command; }
    /** The folder an ls was asked to list, if it was given exactly one; empty for the shell's own. */
    static QString lsFolder(const QString &command);

signals:
    /** What the user types or pastes (a synced tab sends it to all its parts). */
    void input(const QByteArray &bytes);
    /** What the app types itself: only to this shell. */
    void command(const QByteArray &bytes);
    void resized(int columns, int lines);
    /** Files dragged from the computer onto the terminal. */
    void filesDropped(const QStringList &paths);
    void menuRequested(const QPoint &global);
    /** A double click on a name listed by ls: what to do with it is asked to the server first. */
    void nameDoubleClicked(const QString &name, const QString &command, const QPoint &global);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void wheelEvent(QWheelEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    bool focusNextPrevChild(bool) override { return false; }
    void contextMenuEvent(QContextMenuEvent *) override;
    void dragEnterEvent(QDragEnterEvent *) override;
    void dropEvent(QDropEvent *) override;

    /** The folder in an OSC 7 announcement (see the .cpp), or null. */
    static QString announcedFolder(const QString &text);

private:
    bool typesCommands = false;
    /** The folder the shell last announced; forgotten when a command is run, until the next prompt says it again. */
    QString shellFolder;
    QString oscTail;
    using Line = std::vector<VTermScreenCell>;
    /** A point of the text: line counted from the oldest kept one (scrollback first, then the screen), column. */
    struct Point {
        int line = 0, col = 0;
        bool operator<(const Point &o) const { return line < o.line || (line == o.line && col < o.col); }
        bool operator==(const Point &o) const { return line == o.line && col == o.col; }
    };

    static int onDamage(VTermRect, void *);
    static int onMoveCursor(VTermPos, VTermPos, int, void *);
    static int onProp(VTermProp, VTermValue *, void *);
    static int onPushLine(int, const VTermScreenCell *, void *);
    static int onPopLine(int, VTermScreenCell *, void *);
    static void onOutput(const char *, size_t, void *);

    VTermScreenCell cellAt(int line, int col) const;
    QColor colour(VTermColor c, bool foreground) const;
    Point pointAt(const QPoint &p) const;
    QString textBetween(Point a, Point b) const;
    void paste(QString text);
    void copy(bool clipboard);
    int firstShownLine() const { return int(scrollback.size()) - scrolled; }
    QRectF scrollTrack() const;
    QRectF thumb() const;
    void paintScrollBar(QPainter &p);
    void scrollTo(double y);

    VTerm *vt;
    VTermScreen *screen;
    QFont font;
    int cellW = 8, cellH = 16, ascent = 12;
    int rows = 24, cols = 80;
    VTermPos cursor{0, 0};
    bool cursorShown = true;
    std::deque<Line> scrollback;
    int scrolled = 0;  // lines scrolled back from the bottom
    bool selecting = false;
    bool dragging = false;  // the scroll bar's thumb
    double dragOffset = 0;
    Point selStart, selEnd;
    bool altScreen = false;
    bool ghost = false;
    /** While the hidden command runs: what arrives is kept until the mark, then the echo before it is dropped. */
    bool hiding = false;
    QString hidden, reply;
    std::function<void(const QString &)> onFolder;
    /** A name of an ls output under the mouse: its line, columns, text and the command that listed it. */
    struct NameHit {
        int line = -1, from = 0, to = 0;
        QString name, command;
    };
    NameHit nameAt(Point p) const;
    QString lineText(int line) const;
    bool inLsOutput(int y) const;
    QString commandOf(int y) const;
    bool shellReady(int y = -1) const;
    NameHit hover, menuHit;
    void feed(const QString &text);
};
