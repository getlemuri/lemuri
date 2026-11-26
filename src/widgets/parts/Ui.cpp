#include "widgets/parts/Ui.h"

#include "app/tools/Texts.h"
#include "widgets/parts/Chrome.h"
#include "widgets/look/Icons.h"
#include "widgets/look/Backdrop.h"
#include "widgets/look/Theme.h"

#include <QAction>
#include <QComboBox>
#include <QDateTime>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QJsonArray>
#include <QJsonObject>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QTimer>
#include <QVBoxLayout>
#include <QWindow>
#include <cmath>

namespace {

QPushButton *makeButton(const QString &text, const QString &icon, const char *kind, std::function<void()> action) {
    auto *b = new QPushButton(text);
    b->setProperty("kind", kind);
    b->setCursor(Qt::PointingHandCursor);
    QColor c = QString(kind) == "primary" ? QColor(Qt::white) : QString(kind) == "danger" ? Theme::dangerText : Theme::text;
    if (!icon.isEmpty()) {
        b->setIcon(Icons::icon(icon, 13, c));
        b->setIconSize(QSize(13, 13));
    }
    if (action) QObject::connect(b, &QPushButton::clicked, b, [action] { action(); });
    return b;
}
/** A message of a dialog: wrapped at a fixed width, with the height of all its lines asked for at that width. */
QLabel *message(const QString &text, bool selectable) {
    constexpr int WIDTH = 400;
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    if (selectable) l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setFixedWidth(WIDTH);
    l->setMinimumHeight(l->heightForWidth(WIDTH));
    return l;
}
}  // namespace

QString Ui::styleSheet() {
    static const QString sheet = Theme::css(R"(
* { color: @text; }
QToolTip { background: @tip; color: @tipText; border: 1px solid @edge; font-size: 11px; padding: 3px 5px; }
QPushButton { min-height: 24px; max-height: 24px; padding: 0 10px; border: 1px solid @edge; border-radius: 3px;
    background: @control; color: @text; }
QPushButton:hover { background: @controlHover; }
QPushButton:focus { border-color: @accent; }
QPushButton:disabled { color: @disabled; border-color: @controlHover; }
QPushButton[kind="primary"] { background: @accent; border-color: @accentDark; color: white; }
QPushButton[kind="primary"]:hover { background: @accentDark; }
QPushButton[kind="primary"]:disabled { background: @accentOff; border-color: @accentOff; color: @accentOffText; }
QPushButton[kind="danger"] { color: @dangerText; }
QPushButton[kind="destroy"] { background: @criticalFill; border-color: @criticalFillHover; color: white; }
QPushButton[kind="destroy"]:hover { background: @criticalFillHover; }
QPushButton[kind="destroy"]:focus { border-color: white; }
QLineEdit, QSpinBox, QComboBox, QPlainTextEdit, QTextEdit { background: @veilDeep; border: 1px solid @edge; border-radius: 3px;
    padding: 2px 6px; selection-background-color: @textSelection; selection-color: white; }
QLineEdit, QSpinBox, QComboBox { min-height: 20px; }
QLineEdit:focus, QSpinBox:focus, QComboBox:focus, QPlainTextEdit:focus, QTextEdit:focus { border-color: @accent; }
QComboBox { padding-right: 20px; }
QComboBox::drop-down { border: none; width: 20px; subcontrol-origin: padding; subcontrol-position: center right; }
QComboBox::down-arrow { image: url(:/chevron-down.png); width: 12px; height: 12px; }
QComboBox::down-arrow:disabled { image: url(:/chevron-down-off.png); }
QComboBox QAbstractItemView { background: @control; border: 1px solid @edge; selection-background-color: @rowSelected; }
QCheckBox, QRadioButton { spacing: 6px; }
QMenu { background: @control; border: 1px solid @edge; padding: 4px 0; }
QMenu::item { padding: 5px 22px 5px 10px; }
QMenu::item:selected { background: @menuSelected; color: white; }
QMenu::item:disabled { color: @disabled; }
QMenu::icon { padding-left: 8px; }
QMenu::separator { height: 1px; background: @edge; margin: 4px 0; }
QTableWidget, QTableView { background: @veilDeep; border: 1px solid @border; gridline-color: @border; outline: none;
    selection-background-color: @rowSelected; selection-color: @text; }
QTableView::item { border-bottom: 1px solid @border; padding: 0 8px; }
QTableView::item:hover { background: @on; }
QTableView::item:selected { background: @rowSelected; }
QHeaderView::section { background: @veilHead; color: @text; font-weight: bold; border: none;
    border-right: 1px solid @border; border-bottom: 1px solid @border; height: 26px; padding: 0 4px; }
QScrollArea { background: transparent; border: none; }
QScrollArea > QWidget#qt_scrollarea_viewport, QScrollArea > QWidget > QWidget { background: transparent; }
)") + scrollBars();
    return sheet;
}

QString Ui::scrollBars() {
    static const QString sheet = Theme::css(R"(
QScrollBar:vertical { width: 12px; background: transparent; border: none; margin: 0; }
QScrollBar::handle:vertical { min-height: 24px; margin: 2px 2px 2px 1px; border-radius: 4px; border: 1px solid @handleEdge;
    background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 @handleTop, stop:1 @handleBottom); }
QScrollBar:horizontal { height: 12px; background: transparent; border: none; margin: 0; }
QScrollBar::handle:horizontal { min-width: 24px; margin: 1px 2px 2px 2px; border-radius: 4px; border: 1px solid @handleEdge;
    background: @handle; }
QScrollBar::handle:hover { border-color: @handleEdgeLit; }
QScrollBar::add-line, QScrollBar::sub-line { height: 0; width: 0; }
QScrollBar::add-page, QScrollBar::sub-page { background: none; }
)");
    return sheet;
}

QPushButton *Ui::button(const QString &text, const QString &icon, std::function<void()> action) {
    return makeButton(text, icon, "", action);
}

QPushButton *Ui::primary(const QString &text, const QString &icon, std::function<void()> action) {
    return makeButton(text, icon, "primary", action);
}

QPushButton *Ui::danger(const QString &text, const QString &icon, std::function<void()> action) {
    return makeButton(text, icon, "danger", action);
}

IconButton *Ui::action(const QString &icon, const QString &tip, std::function<void()> act) {
    auto *b = new IconButton(icon, 13, QSize(22, 20), tip);
    b->colour = Theme::faint;
    b->hoverColour = Theme::accent;
    b->hoverBackground = Theme::on;
    b->radius = 3;
    if (act) QObject::connect(b, &QAbstractButton::clicked, b, [act] { act(); });
    return b;
}

QString Ui::chipStyle(const QString &tone) {
    // style.css, .dark .status-tone-*: the colour darkened for the background, lightened for the text.
    QString bg = Theme::on.name(), fg = Theme::faint.name();
    if (tone == "ok") bg = Theme::okBack.name(), fg = Theme::okText.name();
    else if (tone == "warn") bg = Theme::warnBack.name(), fg = Theme::warn.name();
    else if (tone == "error") bg = Theme::errorBack.name(), fg = Theme::errorText.name();
    else if (tone == "info") bg = Theme::infoBack.name(), fg = Theme::infoText.name();
    return QString("background: %1; color: %2; border-radius: 3px; padding: 1px 6px; font-size: 11px;").arg(bg, fg);
}

QLabel *Ui::chip(const QString &tone, const QString &text) {
    auto *l = new QLabel(text);
    l->setStyleSheet(Theme::css(chipStyle(tone)));
    l->setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    return l;
}

QString Ui::hostStateText(Host::Reach r) {
    return T(r == Host::Reach::Online ? "host.state.online" : r == Host::Reach::Offline ? "host.state.offline" : "host.state.unknown");
}

QString Ui::sessionStateText(Session::State s) {
    using State = Session::State;
    return T(s == State::Connected ? "session.state.connected" : s == State::Error ? "session.state.error"
             : s == State::Closed ? "session.state.closed" : "session.state.connecting");
}

QLabel *Ui::hostState(Host::Reach r) {
    return chip(r == Host::Reach::Online ? "ok" : r == Host::Reach::Offline ? "error" : "neutral", hostStateText(r));
}

QLabel *Ui::faint(const QString &text) {
    auto *l = new QLabel(text);
    l->setStyleSheet(Theme::css("color: @faint;"));
    return l;
}

QLabel *Ui::code(const QString &text) {
    auto *l = new QLabel(text);
    l->setStyleSheet(Theme::css("font-family: 'DejaVu Sans Mono'; font-size: 11px;"));
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QWidget *Ui::panel(const QString &title, QWidget *body, QWidget *headExtra) {
    auto *p = new QFrame;
    p->setObjectName("panel");
    p->setStyleSheet(Theme::css("#panel { border: 1px solid @border; background: @veil; }"
                     "#panelHead { background: @veilHead; border-bottom: 1px solid @border; }"));
    auto *col = new QVBoxLayout(p);
    col->setContentsMargins(1, 1, 1, 1);
    col->setSpacing(0);
    auto *head = new QWidget;
    head->setObjectName("panelHead");
    head->setAttribute(Qt::WA_StyledBackground);
    auto *row = new QHBoxLayout(head);
    row->setContentsMargins(8, 6, 8, 6);
    auto *t = new QLabel(title);
    t->setStyleSheet(Theme::css("font-weight: bold;"));
    row->addWidget(t);
    row->addStretch(1);
    if (headExtra) row->addWidget(headExtra);
    col->addWidget(head);
    auto *wrap = new QWidget;
    auto *in = new QVBoxLayout(wrap);
    in->setContentsMargins(8, 8, 8, 8);
    in->addWidget(body);
    col->addWidget(wrap, 1);
    return p;
}

QWidget *Ui::bar(const QList<QWidget *> &left, const QList<QWidget *> &right) {
    auto *w = new QWidget;
    w->setObjectName("toolbar");
    w->setAttribute(Qt::WA_StyledBackground);
    w->setStyleSheet(Theme::css("#toolbar { background: @veilHead; border: 1px solid @border; }"));
    auto *row = new QHBoxLayout(w);
    row->setContentsMargins(4, 4, 4, 4);
    row->setSpacing(4);
    for (QWidget *x : left) row->addWidget(x);
    row->addStretch(1);
    for (QWidget *x : right) row->addWidget(x);
    return w;
}

QString Ui::measure(qint64 kb) {
    double v = double(kb);
    const char *unit[] = {"KiB", "MiB", "GiB", "TiB"};
    int i = 0;
    while (v >= 1024 && i < 3) {
        v /= 1024;
        i++;
    }
    return (v >= 100 || i == 0 ? QString::number(qRound64(v)) : QString::number(v, 'f', 1)) + " " + unit[i];
}

QString Ui::dateTime(qint64 ms) {
    return QDateTime::fromMSecsSinceEpoch(ms).toString("dd/MM/yyyy HH:mm:ss");
}

double Ui::quota(const QJsonObject &d) {
    double used = d["usedKb"].toDouble(), free = d["freeKb"].toDouble();
    return used + free == 0 ? 0 : used / (used + free);
}

QJsonObject Ui::fullestDisk(const QJsonObject &metrics) {
    QJsonObject best;
    double q = -1;
    for (const QJsonValue &v : metrics["resources"].toObject()["disks"].toArray()) {
        double x = quota(v.toObject());
        if (x > q) {
            q = x;
            best = v.toObject();
        }
    }
    return best;
}

QColor Ui::level(double q) {
    return q >= 0.9 ? Theme::critical : q >= 0.75 ? Theme::amber : Theme::accent;
}

QString Ui::disksTip(const QJsonObject &m) {
    QString tip;
    for (const QJsonValue &v : m["resources"].toObject()["disks"].toArray()) {
        QJsonObject d = v.toObject();
        tip += QString("%1 %2%  %3\n").arg(d["dot"].toString(), -18).arg(qRound(quota(d) * 100), 3)
                       .arg(T("disk.freeOf", measure(qint64(d["freeKb"].toDouble())), measure(qint64(d["totalKb"].toDouble()))));
    }
    tip += T("metrics.readAt", QDateTime::fromMSecsSinceEpoch(qint64(m["when"].toDouble())).toString("HH:mm:ss"));
    if (m["error"].isString()) tip += "\n" + T("metrics.tip.failed", m["error"].toString());
    return tip;
}

void Ui::revealable(QLineEdit *field) {
    field->setEchoMode(QLineEdit::Password);
    auto *eye = field->addAction(Icons::icon("eye", 16, Theme::faint, 1.8), QLineEdit::TrailingPosition);
    eye->setToolTip(T("password.show"));
    QObject::connect(eye, &QAction::triggered, field, [field, eye] {
        bool hidden = field->echoMode() == QLineEdit::Password;
        field->setEchoMode(hidden ? QLineEdit::Normal : QLineEdit::Password);
        eye->setIcon(Icons::icon(hidden ? "eye-off" : "eye", 16, hidden ? Theme::text : Theme::faint, 1.8));
        eye->setToolTip(T(hidden ? "password.hide" : "password.show"));
    });
}

bool Ui::confirm(QWidget *parent, const QString &title, const QString &text, const QString &action, bool danger) {
    FramedDialog d(parent, title);
    d.body()->addWidget(message(text, false));
    d.buttons(action, danger)->setFocus();
    return d.exec() == QDialog::Accepted;
}

void Ui::warning(QWidget *parent, const QString &title, const QString &text) {
    FramedDialog d(parent, title);
    d.body()->addWidget(message(text, true));
    QPushButton *ok = d.addButton("OK");
    ok->setProperty("kind", "primary");
    QObject::connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    ok->setFocus();
    d.exec();
}

std::optional<QString> Ui::name(QWidget *parent, const QString &title, const QString &label, const QString &start,
                                const QString &action, std::function<QString(const QString &)> check) {
    FramedDialog d(parent, title);
    d.body()->addWidget(new QLabel(label));
    auto *field = new QLineEdit(start);
    field->setMinimumWidth(360);
    d.body()->addWidget(field);
    auto *error = new QLabel;
    error->setStyleSheet(Theme::css("color: @warn;"));
    d.body()->addWidget(error);
    QPushButton *ok = d.buttons(action);
    auto refresh = [&] {
        QString why = check ? check(field->text().trimmed()) : QString();
        error->setText(why);
        ok->setEnabled(why.isEmpty());
    };
    QObject::connect(field, &QLineEdit::textChanged, &d, refresh);
    refresh();
    field->setFocus();
    field->selectAll();
    if (d.exec() != QDialog::Accepted) return std::nullopt;
    return field->text().trimmed();
}

std::optional<QString> Ui::folder(QWidget *parent, const QString &title, const QString &label, const QStringList &choices, const QString &start) {
    FramedDialog d(parent, title);
    d.body()->addWidget(new QLabel(label));
    auto *box = new QComboBox;
    box->setMinimumWidth(360);
    for (const QString &c : choices) box->addItem(c.isEmpty() ? T("tree.datacenter") : c, c);
    box->setCurrentIndex(std::max(0, int(choices.indexOf(start))));
    d.body()->addWidget(box);
    d.buttons(T("common.move"));
    if (d.exec() != QDialog::Accepted) return std::nullopt;
    return box->currentData().toString();
}

// ── UsageBar ──

UsageBar::UsageBar(double quota, const QString &text, int trackWidth, QWidget *parent)
    : QWidget(parent), q(std::clamp(quota, 0.0, 1.0)), text(QString("%1%  %2").arg(qRound(q * 100)).arg(text)), track(trackWidth) {
    setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Fixed);
}

QSize UsageBar::sizeHint() const {
    return {track + 8 + fontMetrics().horizontalAdvance(text) + 2, std::max(16, fontMetrics().height())};
}

void UsageBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    double y = (height() - 8) / 2.0;
    p.setPen(Qt::NoPen);
    p.setBrush(Theme::track);
    p.drawRoundedRect(QRectF(0, y, track, 8), 4, 4);
    if (q > 0) {
        p.setBrush(Ui::level(q));
        p.drawRoundedRect(QRectF(0, y, std::max(8.0, track * q), 8), 4, 4);
    }
    p.setPen(Theme::text);
    p.drawText(QRectF(track + 8, 0, width() - track - 8, height()), Qt::AlignVCenter | Qt::AlignLeft, text);
}

// ── Gauge ──

Gauge::Gauge(const QString &title, bool score, QWidget *parent) : QWidget(parent), title(title), score(score) {
    setMinimumHeight(150);
}

void Gauge::set(std::optional<double> quota, const QString &d) {
    value = quota;
    detail = d;
    update();
}

void Gauge::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QFont bold = font();
    bold.setPixelSize(13);
    bold.setBold(true);
    p.setFont(bold);
    p.setPen(Theme::text);
    p.drawText(QRectF(0, 4, width(), 18), Qt::AlignCenter, title);
    QPointF c(width() / 2.0, 26 + 46);
    double r = 38;
    p.setPen(QPen(Theme::track, 9));
    p.setBrush(Qt::NoBrush);
    p.drawEllipse(c, r, r);
    double q = std::clamp(value.value_or(0), 0.0, 1.0);
    QColor colour = score ? (q < 0.5 ? Theme::critical : q < 0.8 ? Theme::amber : Theme::ok) : Ui::level(q);
    if (value && q > 0) {
        p.setPen(QPen(colour, 9, Qt::SolidLine, Qt::FlatCap));
        p.drawArc(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), 90 * 16, int(-360 * 16 * q));
    }
    QFont big = font();
    big.setPixelSize(17);
    big.setBold(true);
    p.setFont(big);
    p.setPen(Theme::text);
    p.drawText(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r), Qt::AlignCenter, value ? QString("%1%").arg(qRound(q * 100)) : "…");
    QFont small = font();
    small.setPixelSize(12);
    p.setFont(small);
    p.setPen(Theme::faint);
    p.drawText(QRectF(0, c.y() + r + 10, width(), 18), Qt::AlignCenter, detail);
}

// ── FramedDialog ──

FramedDialog::FramedDialog(QWidget *parent, const QString &title) : QDialog(parent) {
    setWindowTitle(title);
    setWindowFlag(Qt::FramelessWindowHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setStyleSheet(Ui::styleSheet() + "QLabel { background: transparent; }");
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    outer->setSpacing(0);

    auto *head = new QWidget;
    head->setFixedHeight(36);
    auto *row = new QHBoxLayout(head);
    row->setContentsMargins(12, 0, 6, 0);
    auto *logo = new QLabel;
    QPixmap pm(":/logo.png");
    qreal ratio = devicePixelRatioF();
    pm = pm.scaled(QSize(18, 18) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pm.setDevicePixelRatio(ratio);
    logo->setPixmap(pm);
    row->addWidget(logo);
    auto *brand = new QLabel("Lemuri");
    brand->setStyleSheet(Theme::css("color: @topFaint;"));
    row->addWidget(brand);
    auto *name = new QLabel(title);
    name->setAlignment(Qt::AlignCenter);
    name->setStyleSheet(Theme::css("font-weight: bold; font-size: 13px;"));
    row->addWidget(name, 1);
    auto *close = new IconButton("close", 13, QSize(30, 26), T("window.close"));
    close->colour = Theme::topFaint;
    close->hoverColour = Theme::text;
    close->hoverBackground = QColor(255, 255, 255, 26);
    connect(close, &QAbstractButton::clicked, this, &QDialog::reject);
    // Same width on both sides, so the title stays in the middle.
    brand->setFixedWidth(std::max(brand->sizeHint().width(), 10));
    row->addWidget(close);
    outer->addWidget(head);

    auto *middle = new QWidget;
    content = new QVBoxLayout(middle);
    content->setContentsMargins(12, 12, 12, 12);
    content->setSpacing(8);
    outer->addWidget(middle, 1);

    foot = new QWidget;
    foot->setObjectName("dialogFoot");
    foot->setAttribute(Qt::WA_StyledBackground);
    foot->setStyleSheet(Theme::css("#dialogFoot { background: @veilHead; border-top: 1px solid @edge; border-bottom-left-radius: 8px; border-bottom-right-radius: 8px; }"));
    auto *r = new QHBoxLayout(foot);
    r->setContentsMargins(10, 8, 10, 8);
    r->setSpacing(8);
    r->addStretch(1);
    buttonRow = r;
    foot->hide();
    outer->addWidget(foot);
}

QPushButton *FramedDialog::addButton(const QString &text, bool left) {
    foot->show();
    auto *b = new QPushButton(text);
    b->setMinimumWidth(84);
    b->setCursor(Qt::PointingHandCursor);
    if (left) buttonRow->insertWidget(0, b);
    else buttonRow->addWidget(b);
    return b;
}

QPushButton *FramedDialog::buttons(const QString &ok, bool danger, const QString &cancel) {
    QPushButton *c = addButton(cancel.isEmpty() ? T("common.cancel") : cancel);
    connect(c, &QPushButton::clicked, this, &QDialog::reject);
    QPushButton *o = addButton(ok);
    // The main choice of a dialog is filled: blue, or red when it deletes something.
    o->setProperty("kind", danger ? "destroy" : "primary");
    o->setDefault(true);
    connect(o, &QPushButton::clicked, this, &QDialog::accept);
    return o;
}

void FramedDialog::showEvent(QShowEvent *e) {
    fitTexts();
    QDialog::showEvent(e);
}

void FramedDialog::resizeEvent(QResizeEvent *e) {
    QDialog::resizeEvent(e);
    // Resized from outside (the window manager): the texts are measured again, once the layout has followed.
    if (!fitting) QTimer::singleShot(0, this, [this] { fitTexts(); });
}

void FramedDialog::fitTexts() {
    if (fitting) return;
    fitting = true;
    for (int round = 0; round < 4; round++) {
        if (layout()) layout()->activate();
        bool changed = false;
        for (QLabel *l : findChildren<QLabel *>()) {
            if (!l->wordWrap() || l->isHidden() || l->width() <= 0) continue;
            int need = l->heightForWidth(l->width());
            if (need > l->minimumHeight()) {
                l->setMinimumHeight(need);
                changed = true;
            }
        }
        if (!changed) break;
        resize(width(), std::max(height(), sizeHint().height()));
        if (layout()) resize(width(), std::max(height(), layout()->totalHeightForWidth(width())));
    }
    fitting = false;
}


void FramedDialog::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    QRectF r = QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5);
    QPainterPath shape;
    shape.addRoundedRect(r, 8, 8);
    // The main window's backdrop, waves and all; the title bar sits on it with nothing of its own.
    Backdrop::paint(p, shape, r);
    p.setPen(QPen(Theme::edge, 1));
    p.drawPath(shape);
}

void FramedDialog::mousePressEvent(QMouseEvent *e) {
    if (e->button() == Qt::LeftButton && e->position().y() < 36 && windowHandle()) windowHandle()->startSystemMove();
}
