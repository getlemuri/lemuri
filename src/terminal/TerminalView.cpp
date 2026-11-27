#include "terminal/TerminalView.h"
#include "widgets/look/Theme.h"

#include <QApplication>
#include <QClipboard>
#include <QFontDatabase>
#include <QFontMetrics>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QKeyEvent>
#include <QMimeData>
#include <QRegularExpression>
#include <QToolTip>
#include <QTimer>
#include <QUrl>
#include <QPainter>
#include <algorithm>

namespace {
constexpr int MAX_SCROLLBACK = 5000;
// Margins: 6 px on the left, 4 above, 2 below; then the 12 px scroll bar on the right.
constexpr int PAD_X = 6, PAD_Y = 4, PAD_BOTTOM = 2, SCROLL_BAR = 12;
const QColor BACKGROUND(0x00, 0x00, 0x00);
const QColor FOREGROUND(0xd6, 0xdd, 0xe6);
const QColor SELECTION(0x26, 0x4f, 0x78);
const QString MARK = QStringLiteral("\x1b]777;lemuri\x07");
const QString DIR = QStringLiteral("\x1b]777;lemuri-pwd;");
// A prompt line ("user@host:~$ command"), an ls command, and the "cd x && " a double click types.
const QRegularExpression PROMPT(R"(^\[?[\w.-]+@[\w.-]+(?::|\s+)[^$#%\]]*\]?[$#%] ?(.*)$)");
const QRegularExpression LS(R"(^(ls|ll|la|l|dir)(\s|$))");
const QRegularExpression CD_THEN(R"(^cd\s.*?&&\s*)");

#ifdef Q_OS_MACOS
// On a Mac Qt reports Command as Control and the Control key as Meta: the Control key is the terminal's Ctrl, and
// Command is the app's (Command+C and Command+V copy and paste, as in Terminal.app).
constexpr Qt::KeyboardModifier CONTROL = Qt::MetaModifier;
#else
constexpr Qt::KeyboardModifier CONTROL = Qt::ControlModifier;
#endif

VTermModifier modifiers(Qt::KeyboardModifiers m) {
    int r = VTERM_MOD_NONE;
    if (m & Qt::ShiftModifier) r |= VTERM_MOD_SHIFT;
    if (m & Qt::AltModifier) r |= VTERM_MOD_ALT;
    if (m & CONTROL) r |= VTERM_MOD_CTRL;
    return VTermModifier(r);
}
}  // namespace

TerminalView::TerminalView(QWidget *parent) : QWidget(parent) {
    // A command run (ssh to another server, a cd in a shell that does not announce it): the folder is not known any
    // more, until the next prompt announces it again.
    connect(this, &TerminalView::input, this, [this](const QByteArray &bytes) {
        if (bytes.contains('\r')) shellFolder = QString();
    });
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setAttribute(Qt::WA_InputMethodEnabled);
    setCursor(Qt::IBeamCursor);
    setAcceptDrops(true);
    setMouseTracking(true);

    // DejaVu Sans Mono; on a Mac (no DejaVu) the fonts of its Terminal.
    font.setFamilies({"DejaVu Sans Mono", "Menlo", "SF Mono", "Monaco"});
    font.setStyleHint(QFont::Monospace);
    font.setFixedPitch(true);
    font.setPixelSize(13);
    QFontMetrics fm(font);
    cellW = fm.horizontalAdvance(QLatin1Char('M'));
    cellH = fm.height();
    ascent = fm.ascent();
    // Cells are whole pixels, glyphs are not (7.8 px at 13 px): without this a run of text comes out shorter than its
    // cells, leaving gaps between coloured parts and drifting away from the cursor along the line.
    font.setLetterSpacing(QFont::AbsoluteSpacing, cellW - QFontMetricsF(font).horizontalAdvance(QLatin1Char('M')));

    vt = vterm_new(rows, cols);
    vterm_set_utf8(vt, 1);
    vterm_output_set_callback(vt, &TerminalView::onOutput, this);
    screen = vterm_obtain_screen(vt);

    static VTermScreenCallbacks callbacks = [] {
        VTermScreenCallbacks c{};
        c.damage = &TerminalView::onDamage;
        c.movecursor = &TerminalView::onMoveCursor;
        c.settermprop = &TerminalView::onProp;
        c.sb_pushline = &TerminalView::onPushLine;
        c.sb_popline = &TerminalView::onPopLine;
        return c;
    }();
    vterm_screen_set_callbacks(screen, &callbacks, this);
    vterm_screen_enable_altscreen(screen, 1);
    VTermColor fg, bg;
    vterm_color_rgb(&fg, FOREGROUND.red(), FOREGROUND.green(), FOREGROUND.blue());
    vterm_color_rgb(&bg, BACKGROUND.red(), BACKGROUND.green(), BACKGROUND.blue());
    vterm_screen_set_default_colors(screen, &fg, &bg);
    VTermState *state = vterm_obtain_state(vt);
    for (int i = 0; i < 16; i++) {
        VTermColor c;
        const QColor &q = Theme::terminalPalette[i];
        vterm_color_rgb(&c, q.red(), q.green(), q.blue());
        vterm_state_set_palette_color(state, i, &c);
    }
    vterm_state_set_bold_highbright(state, 1);
    vterm_screen_reset(screen, 1);
}

TerminalView::~TerminalView() {
    vterm_free(vt);
}

/**
 * The folder a shell announces with OSC 7 (ESC ] 7 ; file://host/path, ended by BEL or ESC \): bash's vte.sh, fish,
 * zsh and others send it at every prompt. The last one in `text`, or null.
 */
QString TerminalView::announcedFolder(const QString &text) {
    static const QRegularExpression osc7("\x1b\\]7;file://[^/\x07\x1b]*(/[^\x07\x1b]*)(?:\x07|\x1b\\\\)");
    QString found;
    for (auto it = osc7.globalMatch(text); it.hasNext();) found = QUrl::fromPercentEncoding(it.next().captured(1).toUtf8());
    return found;
}

void TerminalView::write(const QString &incoming) {
    QString d = incoming;
    // Kept across writes: the sequence may arrive cut in two.
    oscTail = (oscTail + d).right(4096);
    QString announced = announcedFolder(oscTail);
    if (!announced.isNull()) {
        shellFolder = announced;
        oscTail.clear();
    }
    if (hiding) {
        hidden += d;
        qsizetype i = hidden.indexOf(MARK);
        if (i < 0) return;
        // The cursor is still on the prompt line, where the echo would have appeared: the shell's new prompt takes its place.
        d = "\r\x1b[K" + hidden.mid(i + MARK.size());
        hiding = false;
        hidden.clear();
    }
    if (onFolder) {
        reply += d;
        qsizetype i = reply.indexOf(DIR);
        qsizetype j = i < 0 ? -1 : reply.indexOf(QChar(7), i);
        if (i >= 0 && j < 0) return;  // the reply is cut in half: wait for the rest
        d = reply;
        reply.clear();
        if (i >= 0) {
            QString folder = d.mid(i + DIR.size(), j - i - DIR.size());
            d = d.left(i) + d.mid(j + 1);
            auto r = std::move(onFolder);
            onFolder = nullptr;
            // Afterwards: the receiver may open a window, and meanwhile the terminal must finish writing.
            QTimer::singleShot(0, this, [r, folder] { r(folder); });
        }
    }
    feed(d);
}

void TerminalView::feed(const QString &text) {
    QByteArray bytes = text.toUtf8();
    vterm_input_write(vt, bytes.constData(), size_t(bytes.size()));
    vterm_screen_flush_damage(screen);
    // New output brings the view back to the bottom, as in every terminal.
    scrolled = 0;
    hover = NameHit{};
}

// ── libvterm callbacks ──

int TerminalView::onDamage(VTermRect, void *self) {
    static_cast<TerminalView *>(self)->update();
    return 1;
}

int TerminalView::onMoveCursor(VTermPos pos, VTermPos, int visible, void *self) {
    auto *t = static_cast<TerminalView *>(self);
    t->cursor = pos;
    t->cursorShown = visible;
    t->update();
    return 1;
}

int TerminalView::onProp(VTermProp prop, VTermValue *value, void *self) {
    auto *t = static_cast<TerminalView *>(self);
    if (prop == VTERM_PROP_CURSORVISIBLE) t->cursorShown = value->boolean;
    else if (prop == VTERM_PROP_ALTSCREEN) t->altScreen = value->boolean;
    else if (prop == VTERM_PROP_TITLE) t->setWindowTitle(QString::fromUtf8(value->string.str, int(value->string.len)));
    return 1;
}

int TerminalView::onPushLine(int columns, const VTermScreenCell *cells, void *self) {
    auto *t = static_cast<TerminalView *>(self);
    t->scrollback.emplace_back(cells, cells + columns);
    if (int(t->scrollback.size()) > MAX_SCROLLBACK) t->scrollback.pop_front();
    return 1;
}

int TerminalView::onPopLine(int columns, VTermScreenCell *cells, void *self) {
    auto *t = static_cast<TerminalView *>(self);
    if (t->scrollback.empty()) return 0;
    const Line &l = t->scrollback.back();
    for (int i = 0; i < columns; i++) {
        if (i < int(l.size())) cells[i] = l[size_t(i)];
        else {
            cells[i] = VTermScreenCell{};
            cells[i].width = 1;
            cells[i].fg = l.empty() ? VTermColor{} : l[0].fg;
            cells[i].bg = l.empty() ? VTermColor{} : l[0].bg;
        }
    }
    t->scrollback.pop_back();
    // A taller screen takes lines back from the scrollback: the view must not stay scrolled past its new top.
    t->scrolled = std::min(t->scrolled, int(t->scrollback.size()));
    return 1;
}

void TerminalView::onOutput(const char *s, size_t len, void *self) {
    emit static_cast<TerminalView *>(self)->input(QByteArray(s, qsizetype(len)));
}

// ── Drawing ──

VTermScreenCell TerminalView::cellAt(int line, int col) const {
    int sb = int(scrollback.size());
    if (line < sb) {
        // A line before the first one (never expected) reads as blank instead of outside the scrollback.
        if (line >= 0 && col < int(scrollback[size_t(line)].size())) return scrollback[size_t(line)][size_t(col)];
        VTermScreenCell empty{};
        empty.width = 1;
        vterm_color_rgb(&empty.fg, FOREGROUND.red(), FOREGROUND.green(), FOREGROUND.blue());
        vterm_color_rgb(&empty.bg, BACKGROUND.red(), BACKGROUND.green(), BACKGROUND.blue());
        empty.fg.type |= VTERM_COLOR_DEFAULT_FG;
        empty.bg.type |= VTERM_COLOR_DEFAULT_BG;
        return empty;
    }
    VTermScreenCell c{};
    vterm_screen_get_cell(screen, VTermPos{line - sb, col}, &c);
    return c;
}

QColor TerminalView::colour(VTermColor c, bool foreground) const {
    if (foreground ? VTERM_COLOR_IS_DEFAULT_FG(&c) : VTERM_COLOR_IS_DEFAULT_BG(&c)) return foreground ? FOREGROUND : BACKGROUND;
    vterm_screen_convert_color_to_rgb(screen, &c);
    return QColor(c.rgb.red, c.rgb.green, c.rgb.blue);
}

void TerminalView::paintEvent(QPaintEvent *) {
    QPainter p(this);
    // In ghost mode the window behind has already painted its see-through black here.
    if (!ghost) p.fillRect(rect(), BACKGROUND);
    p.setFont(font);
    Point a = std::min(selStart, selEnd), b = std::max(selStart, selEnd);
    bool anySelection = !(a == b);
    int top = firstShownLine();

    for (int r = 0; r < rows; r++) {
        int line = top + r;
        int y = PAD_Y + r * cellH;
        // Runs of cells with the same look are drawn as one string: far fewer calls than one per cell.
        int col = 0;
        while (col < cols) {
            VTermScreenCell cell = cellAt(line, col);
            bool reverse = cell.attrs.reverse;
            Point here{line, col};
            bool selected = anySelection && !(here < a) && here < b;
            QColor fg = colour(cell.fg, true), bg = colour(cell.bg, false);
            if (reverse) std::swap(fg, bg);
            if (selected) bg = SELECTION;
            bool bold = cell.attrs.bold, italic = cell.attrs.italic, underline = cell.attrs.underline;

            QString text;
            int start = col;
            while (col < cols) {
                VTermScreenCell c = cellAt(line, col);
                Point pt{line, col};
                bool sel = anySelection && !(pt < a) && pt < b;
                QColor f = colour(c.fg, true), g = colour(c.bg, false);
                if (c.attrs.reverse) std::swap(f, g);
                if (sel) g = SELECTION;
                if (col > start && (f != fg || g != bg || bool(c.attrs.bold) != bold || bool(c.attrs.italic) != italic
                        || bool(c.attrs.underline) != underline || c.width > 1)) break;
                if (c.chars[0] == 0) text += QLatin1Char(' ');
                else for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && c.chars[i]; i++) text += QString::fromUcs4(reinterpret_cast<const char32_t *>(&c.chars[i]), 1);
                col += std::max(1, int(c.width));
                if (c.width > 1) break;
            }
            int x = PAD_X + start * cellW, w = (col - start) * cellW;
            if (bg != BACKGROUND) p.fillRect(x, y, w, cellH, bg);
            QFont f = font;
            f.setBold(bold);
            f.setItalic(italic);
            f.setUnderline(underline);
            p.setFont(f);
            p.setPen(fg);
            p.drawText(x, y + ascent, text);
        }
    }

    // The cursor: a block, hollow when another window has the keyboard.
    int cursorRow = int(scrollback.size()) + cursor.row - top;
    if (cursorShown && cursorRow >= 0 && cursorRow < rows) {
        QRect c(PAD_X + cursor.col * cellW, PAD_Y + cursorRow * cellH, cellW, cellH);
        if (hasFocus()) {
            p.fillRect(c, FOREGROUND);
            VTermScreenCell under = cellAt(int(scrollback.size()) + cursor.row, cursor.col);
            if (under.chars[0]) {
                p.setFont(font);
                p.setPen(BACKGROUND);
                p.drawText(c.x(), c.y() + ascent, QString::fromUcs4(reinterpret_cast<const char32_t *>(&under.chars[0]), 1));
            }
        } else {
            p.setPen(FOREGROUND);
            p.drawRect(c.adjusted(0, 0, -1, -1));
        }
    }
    // The name of an ls under the mouse, underlined.
    if (hover.line >= top && hover.line < top + rows) {
        int y = PAD_Y + (hover.line - top) * cellH + ascent + 2;
        p.fillRect(QRect(PAD_X + hover.from * cellW, y, (hover.to - hover.from) * cellW, 1), FOREGROUND);
    }
    paintScrollBar(p);
}

QRectF TerminalView::thumb() const {
    QRectF track = scrollTrack();
    double total = double(scrollback.size()) + rows;
    double h = std::max(24.0, track.height() * rows / total);
    double top = track.top() + (track.height() - h) * (scrollback.empty() ? 1.0 : 1.0 - double(scrolled) / double(scrollback.size()));
    return QRectF(track.left(), top, track.width(), h);
}

void TerminalView::paintScrollBar(QPainter &p) {
    // Part of the terminal: nothing while there is nothing to scroll back to; otherwise a thin, faint line at the
    // right edge, wider and brighter under the mouse or while dragged.
    if (scrollback.empty()) return;
    QRectF whole = thumb();
    bool lit = dragging || whole.contains(mapFromGlobal(QCursor::pos()));
    double w = lit ? 7 : 3;
    QRectF t(whole.right() - 2 - w, whole.top() + 2, w, whole.height() - 4);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, lit ? 80 : 34));
    p.drawRoundedRect(t, w / 2, w / 2);
}

QRectF TerminalView::scrollTrack() const {
    return QRectF(width() - SCROLL_BAR + 1, scrollTop, SCROLL_BAR - 1, height() - scrollTop);
}

void TerminalView::scrollTo(double y) {
    QRectF track = scrollTrack(), t = thumb();
    double room = track.height() - t.height();
    if (room <= 0 || scrollback.empty()) return;
    double f = std::clamp((y - track.top() - dragOffset) / room, 0.0, 1.0);
    scrolled = int(std::lround((1.0 - f) * double(scrollback.size())));
    update();
}

void TerminalView::resizeEvent(QResizeEvent *) {
    int c = std::max(2, (width() - PAD_X - SCROLL_BAR) / cellW);
    int r = std::max(1, (height() - PAD_Y - PAD_BOTTOM) / cellH);
    if (c == cols && r == rows) return;
    cols = c;
    rows = r;
    vterm_set_size(vt, rows, cols);
    vterm_screen_flush_damage(screen);
    emit resized(cols, rows);
    update();
}

// ── Keyboard ──

void TerminalView::keyPressEvent(QKeyEvent *e) {
    Qt::KeyboardModifiers m = e->modifiers();
#ifdef Q_OS_MACOS
    if (m & Qt::ControlModifier) {
        if (e->key() == Qt::Key_C) copy(true);
        else if (e->key() == Qt::Key_V) paste(QApplication::clipboard()->text(QClipboard::Clipboard));
        else e->ignore();  // the app's shortcuts
        return;
    }
#else
    bool ctrlShift = (m & Qt::ControlModifier) && (m & Qt::ShiftModifier);
    // Copy and paste as in the Linux terminals; Ctrl+C stays the interrupt.
    if (ctrlShift && e->key() == Qt::Key_C) { copy(true); return; }
    if ((ctrlShift && e->key() == Qt::Key_V) || ((m & Qt::ShiftModifier) && e->key() == Qt::Key_Insert)) {
        paste(QApplication::clipboard()->text(QClipboard::Clipboard));
        return;
    }
#endif
    if ((m & Qt::ShiftModifier) && (e->key() == Qt::Key_PageUp || e->key() == Qt::Key_PageDown)) {
        int step = e->key() == Qt::Key_PageUp ? rows - 1 : -(rows - 1);
        scrolled = std::clamp(scrolled + step, 0, int(scrollback.size()));
        update();
        return;
    }

    VTermModifier mod = modifiers(m);
    VTermKey key = VTERM_KEY_NONE;
    switch (e->key()) {
        case Qt::Key_Return: case Qt::Key_Enter: key = VTERM_KEY_ENTER; break;
        case Qt::Key_Tab: key = VTERM_KEY_TAB; break;
        case Qt::Key_Backtab: key = VTERM_KEY_TAB; mod = VTermModifier(mod | VTERM_MOD_SHIFT); break;
        case Qt::Key_Backspace: key = VTERM_KEY_BACKSPACE; break;
        case Qt::Key_Escape: key = VTERM_KEY_ESCAPE; break;
        case Qt::Key_Up: key = VTERM_KEY_UP; break;
        case Qt::Key_Down: key = VTERM_KEY_DOWN; break;
        case Qt::Key_Left: key = VTERM_KEY_LEFT; break;
        case Qt::Key_Right: key = VTERM_KEY_RIGHT; break;
        case Qt::Key_Insert: key = VTERM_KEY_INS; break;
        case Qt::Key_Delete: key = VTERM_KEY_DEL; break;
        case Qt::Key_Home: key = VTERM_KEY_HOME; break;
        case Qt::Key_End: key = VTERM_KEY_END; break;
        case Qt::Key_PageUp: key = VTERM_KEY_PAGEUP; break;
        case Qt::Key_PageDown: key = VTERM_KEY_PAGEDOWN; break;
        default:
            if (e->key() >= Qt::Key_F1 && e->key() <= Qt::Key_F12) key = VTermKey(VTERM_KEY_FUNCTION(e->key() - Qt::Key_F1 + 1));
    }
    scrolled = 0;
    if (key != VTERM_KEY_NONE) {
        vterm_keyboard_key(vt, key, mod);
        return;
    }
    // Ctrl+letter: libvterm turns the letter into its control code (Ctrl+C is 0x03).
    if ((m & CONTROL) && e->key() >= Qt::Key_A && e->key() <= Qt::Key_Z) {
        vterm_keyboard_unichar(vt, uint32_t('a' + (e->key() - Qt::Key_A)), VTermModifier(mod & ~VTERM_MOD_SHIFT));
        return;
    }
    QString text = e->text();
    if (text.isEmpty()) return;
    // Shift is already in the character itself.
    VTermModifier charMod = VTermModifier(mod & ~VTERM_MOD_SHIFT);
#ifdef Q_OS_MACOS
    // Option types characters on a Mac (@ # [ ] { } on many layouts): what it typed goes as it is, not as Esc + it.
    charMod = VTermModifier(charMod & ~VTERM_MOD_ALT);
#endif
    for (char32_t c : text.toUcs4()) {
        if (c < 0x20 && !(m & CONTROL)) continue;
        vterm_keyboard_unichar(vt, uint32_t(c), charMod);
    }
}

// ── Mouse: scrolling back, selecting, the middle button pastes ──

void TerminalView::wheelEvent(QWheelEvent *e) {
    int steps = e->angleDelta().y() / 40;
    scrolled = std::clamp(scrolled + steps, 0, int(scrollback.size()));
    update();
}

TerminalView::Point TerminalView::pointAt(const QPoint &p) const {
    int r = std::clamp((p.y() - PAD_Y) / cellH, 0, rows - 1);
    int c = std::clamp((p.x() - PAD_X + cellW / 2) / cellW, 0, cols);
    return {firstShownLine() + r, c};
}

void TerminalView::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::MiddleButton) {
        paste(QApplication::clipboard()->text(QClipboard::Selection));
        return;
    }
    if (e->button() != Qt::LeftButton) return;
    if (e->pos().x() >= width() - SCROLL_BAR && e->pos().y() >= scrollTop) {
        QRectF t = thumb();
        dragging = true;
        dragOffset = t.contains(e->pos()) ? e->pos().y() - t.top() : t.height() / 2;
        scrollTo(e->pos().y());
        return;
    }
    selecting = true;
    selStart = selEnd = pointAt(e->pos());
    update();
}

void TerminalView::mouseMoveEvent(QMouseEvent *e) {
    if (dragging) { scrollTo(e->pos().y()); return; }
    if (!(e->buttons() & Qt::LeftButton)) {
        // The names of an ls are underlined under the mouse, with a hand: a double click opens them.
        NameHit h = typesCommands && e->pos().x() < width() - SCROLL_BAR ? nameAt(pointAt(e->pos())) : NameHit{};
        if (h.line >= 0 && !shellReady(h.line)) h = NameHit{};
        if (h.line != hover.line || h.from != hover.from) {
            hover = h;
            setCursor(h.line >= 0 ? Qt::PointingHandCursor : Qt::IBeamCursor);
            update();
        }
    }
    if (!selecting) return;
    selEnd = pointAt(e->pos());
    update();
}

void TerminalView::mouseReleaseEvent(QMouseEvent *e) {
    if (dragging) { dragging = false; update(); return; }
    if (e->button() != Qt::LeftButton || !selecting) return;
    selecting = false;
    selEnd = pointAt(e->pos());
    // Selected text becomes the primary selection, as in every Linux terminal.
    if (!(selStart == selEnd)) copy(false);
    update();
}

void TerminalView::mouseDoubleClickEvent(QMouseEvent *e) {
    Point p = pointAt(e->pos());
    if (typesCommands && e->button() == Qt::LeftButton) {
        NameHit h = nameAt(p);
        if (h.line >= 0 && shellReady(h.line)) {
            selStart = selEnd;
            emit nameDoubleClicked(h.name, h.command, e->globalPosition().toPoint());
            return;
        }
        // On the empty space at the prompt: the shell lists its folder.
        if (scrolled == 0 && p.line >= int(scrollback.size()) + cursor.row && shellReady()) {
            typeCommand("ls");
            return;
        }
    }
    // A word: the run of non-blank characters under the mouse.
    auto blank = [&](int col) {
        VTermScreenCell c = cellAt(p.line, col);
        return c.chars[0] == 0 || c.chars[0] == ' ';
    };
    int col = std::min(p.col, cols - 1);
    if (blank(col)) return;
    int from = col, to = col;
    while (from > 0 && !blank(from - 1)) from--;
    while (to < cols - 1 && !blank(to + 1)) to++;
    selStart = {p.line, from};
    selEnd = {p.line, to + 1};
    copy(false);
    update();
}

QString TerminalView::textBetween(Point a, Point b) const {
    if (b < a) std::swap(a, b);
    QString out;
    for (int line = a.line; line <= b.line; line++) {
        int from = line == a.line ? a.col : 0, to = line == b.line ? b.col : cols;
        QString l;
        for (int col = from; col < to; col++) {
            VTermScreenCell c = cellAt(line, col);
            if (c.chars[0] == 0) l += QLatin1Char(' ');
            else for (int i = 0; i < VTERM_MAX_CHARS_PER_CELL && c.chars[i]; i++) l += QString::fromUcs4(reinterpret_cast<const char32_t *>(&c.chars[i]), 1);
            if (c.width > 1) col += c.width - 1;
        }
        while (l.endsWith(QLatin1Char(' '))) l.chop(1);
        out += l;
        if (line < b.line) out += QLatin1Char('\n');
    }
    return out;
}

void TerminalView::copy(bool clipboard) {
    QString t = textBetween(selStart, selEnd);
    if (t.isEmpty()) return;
    QApplication::clipboard()->setText(t, QClipboard::Selection);
    if (clipboard) QApplication::clipboard()->setText(t, QClipboard::Clipboard);
}

void TerminalView::paste(QString text) {
    if (text.isEmpty()) return;
    // No control characters from the clipboard (an ESC could end bracketed paste and type commands); new lines as Enter.
    QString clean;
    for (QChar c : text) {
        ushort u = c.unicode();
        if (u == '\n') clean += QLatin1Char('\r');
        else if (u == '\t' || u == '\r' || (u >= 0x20 && u != 0x7f && !(u >= 0x80 && u < 0xa0))) clean += c;
    }
    scrolled = 0;
    // libvterm adds the bracketed paste marks when the shell asked for them.
    vterm_keyboard_start_paste(vt);
    for (char32_t c : clean.toUcs4()) vterm_keyboard_unichar(vt, uint32_t(c), VTERM_MOD_NONE);
    vterm_keyboard_end_paste(vt);
}

void TerminalView::pasteClipboard() {
    paste(QApplication::clipboard()->text(QClipboard::Clipboard));
}

QPoint TerminalView::whereIs(const QString &text) const {
    for (int r = rows - 1; r >= 0; r--) {
        qsizetype x = lineText(firstShownLine() + r).indexOf(text);
        if (x >= 0) return {PAD_X + int((x + text.size() / 2.0) * cellW), PAD_Y + r * cellH + cellH / 2};
    }
    return {-1, -1};
}

QString TerminalView::lineText(int line) const {
    return textBetween({line, 0}, {line, cols});
}

/** Is line y in the output of an ls? Walk back up to the prompt that produced it. */
bool TerminalView::inLsOutput(int y) const {
    if (PROMPT.match(lineText(y)).hasMatch()) return false;
    for (int i = y - 1, n = 0; i >= 0 && n < 300; i--, n++) {
        auto m = PROMPT.match(lineText(i));
        if (m.hasMatch()) {
            QString command = m.captured(1).trimmed();
            command.remove(CD_THEN);
            return command.isEmpty() || LS.match(command).hasMatch();
        }
    }
    return false;
}

/** The command whose output line y is in: the one on the prompt above it. */
QString TerminalView::commandOf(int y) const {
    for (int i = y - 1, n = 0; i >= 0 && n < 300; i--, n++) {
        auto m = PROMPT.match(lineText(i));
        if (m.hasMatch()) return m.captured(1).trimmed();
    }
    return QString();
}

/**
 * The shell waits for a command: the cursor line is a prompt with nothing typed after it, and no full-screen
 * program runs. With `y` (the line of a clicked name) the name must come from the last command, or from an
 * older ls at the same prompt as the current one (same user, host and folder).
 */
bool TerminalView::shellReady(int y) const {
    if (altScreen) return false;
    int cursorLine = int(scrollback.size()) + cursor.row;
    QString now = lineText(cursorLine);
    auto m = PROMPT.match(now);
    if (!m.hasMatch() || !m.captured(1).trimmed().isEmpty()) return false;
    if (y < 0) return true;
    if (y >= cursorLine) return false;
    bool last = true;
    for (int j = y + 1; j < cursorLine && last; j++)
        if (PROMPT.match(lineText(j)).hasMatch()) last = false;
    if (last) return true;
    auto promptOf = [](const QString &line, const QRegularExpressionMatch &x) { return line.left(line.size() - x.captured(1).size()).trimmed(); };
    for (int i = y - 1, n = 0; i >= 0 && n < 300; i--, n++) {
        QString t = lineText(i);
        auto p = PROMPT.match(t);
        if (p.hasMatch()) return promptOf(t, p) == promptOf(now, m);
    }
    return false;
}

TerminalView::NameHit TerminalView::nameAt(Point p) const {
    NameHit hit;
    if (altScreen || p.line >= int(scrollback.size()) + rows || !inLsOutput(p.line)) return hit;
    QString t = lineText(p.line);
    auto found = [&](const QString &name, int from) {
        if (name.isEmpty() || from < 0 || p.col < from || p.col >= from + name.size()) return false;
        // xterm puts in the word the ':' of "ls -R" and the marks of "ls -F"; ls quotes names with special characters.
        QString n = name;
        n.remove(QRegularExpression("[:*@=|]+$"));
        if (n.size() > 1 && (n[0] == '\'' || n[0] == '"') && n.back() == n[0]) n = n.mid(1, n.size() - 2);
        if (n.endsWith('/') && n.size() > 1) n.chop(1);
        for (QChar c : n)
            if (c.category() == QChar::Other_Control) return false;
        hit = {p.line, from, from + int(name.size()), n, commandOf(p.line)};
        return !n.isEmpty();
    };
    // ls -l: permissions, links, user, group, size, date... the name is the last part ("name -> target" for links).
    static const QRegularExpression longFormat(R"(^[-dlcbps][-rwxsStT]{9}[.+@]?\s+\d+\s+\S+\s+\S+\s+[\d,]+\s+\S+\s+\S+\s+\S+\s+(.+)$)");
    auto l = longFormat.match(t);
    if (l.hasMatch()) {
        QString name = l.captured(1).split(" -> ").first();
        found(name, int(l.capturedStart(1)));
        return hit;
    }
    if (QRegularExpression(R"(^totale?\s+\d)").match(t).hasMatch()) return hit;
    static const QRegularExpression word(R"('[^']*'|"[^"]*"|\S+)");
    for (auto it = word.globalMatch(t); it.hasNext();) {
        auto w = it.next();
        if (found(w.captured(0), int(w.capturedStart()))) return hit;
    }
    return NameHit{};
}

QString TerminalView::lsFolder(const QString &command) {
    QString c = command.trimmed();
    c.remove(CD_THEN);
    static const QRegularExpression words(R"('[^']*'|"[^"]*"|\S+)");
    QString only;
    bool first = true;
    for (auto it = words.globalMatch(c); it.hasNext(); first = false) {
        QString w = it.next().captured(0);
        if (first || w.startsWith('-')) continue;
        if (!only.isEmpty()) return QString();
        only = w;
    }
    if (only.size() > 1 && (only[0] == '\'' || only[0] == '"') && only.back() == only[0]) only = only.mid(1, only.size() - 2);
    return only;
}

bool TerminalView::typeCommand(const QString &c) {
    if (!typesCommands || !shellReady()) return false;
    emit command((" " + c + "\r").toUtf8());
    return true;
}

void TerminalView::hint(const QString &text, const QPoint &global) {
    QToolTip::showText(global + QPoint(12, 12), text, this, QRect(), 4000);
}

void TerminalView::currentFolder(std::function<void(const QString &)> then) {
    // Announced by the shell since the last command: nothing to type.
    if (!shellFolder.isNull() && shellReady()) {
        QString f = shellFolder;
        QTimer::singleShot(0, this, [then, f] { then(f); });
        return;
    }
    if (!typesCommands || onFolder || hiding || !shellReady()) {
        then(QString());
        return;
    }
    onFolder = then;
    reply.clear();
    hiding = true;
    hidden.clear();
    emit command(QByteArray(" printf '\\033]777;lemuri\\007'; printf '\\033]777;lemuri-pwd;%s\\007' \"$PWD\"\r"));
    // No mark within 2 seconds: everything is shown, so nothing is lost.
    QTimer::singleShot(2000, this, [this] {
        if (!hiding) return;
        hiding = false;
        QString all = hidden;
        hidden.clear();
        feed(all);
    });
    QTimer::singleShot(3000, this, [this] {
        if (!onFolder) return;
        auto r = std::move(onFolder);
        onFolder = nullptr;
        QString rest = reply;
        reply.clear();
        if (!rest.isEmpty()) feed(rest);
        r(QString());
    });
}

void TerminalView::contextMenuEvent(QContextMenuEvent *e) {
    menuHit = nameAt(pointAt(e->pos()));
    if (menuHit.line >= 0 && !shellReady(menuHit.line)) menuHit = NameHit{};
    emit menuRequested(e->globalPos());
}

void TerminalView::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasUrls()) e->acceptProposedAction();
}

void TerminalView::dropEvent(QDropEvent *e) {
    QStringList paths;
    for (const QUrl &u : e->mimeData()->urls())
        if (u.isLocalFile()) paths << u.toLocalFile();
    if (paths.isEmpty()) return;
    e->acceptProposedAction();
    // After the drop: the confirmation must not open during the drag.
    QTimer::singleShot(0, this, [this, paths] { emit filesDropped(paths); });
}

void TerminalView::setScrollTop(int px) {
    if (scrollTop == px) return;
    scrollTop = px;
    update();
}

void TerminalView::setGhost(bool on) {
    if (ghost == on) return;
    ghost = on;
    setAttribute(Qt::WA_OpaquePaintEvent, !on);
    update();
}
