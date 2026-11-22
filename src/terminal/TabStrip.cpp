#include "terminal/TabStrip.h"

#include "widgets/look/Icons.h"
#include "widgets/look/Theme.h"

#include <QHelpEvent>
#include <QMouseEvent>
#include <QToolTip>
#include <QPainter>
#include <QPainterPath>

namespace {
constexpr double MAX_TITLE = 180;

QFont tabFont(const QFont &base, bool bold) {
    QFont f = base;
    f.setPixelSize(12);
    f.setBold(bold);
    return f;
}
}  // namespace

TabStrip::TabStrip(QWidget *parent) : QWidget(parent) {
    setFixedHeight(23);
    setMouseTracking(true);
}

void TabStrip::setTabs(const QList<Item> &items, int current) {
    tabs.clear();
    for (const Item &i : items) tabs.append({i.title, i.state, i.tip});
    active = current;
    update();
}

bool TabStrip::event(QEvent *e) {
    if (e->type() == QEvent::ToolTip) {
        auto *h = static_cast<QHelpEvent *>(e);
        QList<Place> places = layoutTabs(nullptr);
        for (int i = 0; i < places.size(); i++)
            if (places[i].tab.contains(h->pos()) && !tabs[i].tip.isEmpty()) {
                QToolTip::showText(h->globalPos(), tabs[i].tip, this);
                return true;
            }
        QToolTip::hideText();
        return true;
    }
    return QWidget::event(e);
}

int TabStrip::add(const QString &title) {
    tabs.append({title, State::Connecting, QString()});
    update();
    return int(tabs.size()) - 1;
}

void TabStrip::remove(int i) {
    tabs.removeAt(i);
    if (active >= int(tabs.size())) active = int(tabs.size()) - 1;
    else if (i < active) active--;
    update();
}

void TabStrip::setCurrent(int i) {
    if (i == active) return;
    active = i;
    update();
    emit currentChanged(i);
}

void TabStrip::setTitle(int i, const QString &title) {
    tabs[i].title = title;
    update();
}

void TabStrip::setState(int i, State s) {
    tabs[i].state = s;
    update();
}

QList<TabStrip::Place> TabStrip::layoutTabs(QRectF *plus) const {
    QList<Place> out;
    double x = 0;
    for (int i = 0; i < tabs.size(); i++) {
        QFontMetricsF fm(tabFont(font(), i == active));
        double title = std::min(MAX_TITLE, fm.horizontalAdvance(tabs[i].title));
        // padding 8, icon 13, gap 5, title, gap 5, close 18, padding 1
        double w = 8 + 13 + 5 + title + 5 + 18 + 1;
        QRectF tab(x, 0, w, height() - 1);
        out.append({tab, QRectF(tab.right() - 1 - 18, (height() - 18) / 2.0, 18, 18)});
        x += w;
    }
    if (plus) *plus = QRectF(x + 4, (height() - 18) / 2.0, 20, 18);
    return out;
}

void TabStrip::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    if (!ghost) {
        p.fillRect(rect(), Theme::side);
        // The line under the row, which the active tab stands on.
        p.fillRect(QRect(0, height() - 1, width(), 1), Theme::border);
    }

    QRectF plus;
    QList<Place> places = layoutTabs(&plus);
    for (int i = 0; i < places.size(); i++) {
        const Place &pl = places[i];
        bool on = i == active, hover = pl.tab.contains(mouse);
        if (on || hover) {
            // Top left as the corner of the panel around it (5), top right rounded off (9), square at the bottom.
            QPainterPath shape;
            QRectF r = pl.tab.adjusted(0, 0, 0, on ? 1 : 0);
            shape.moveTo(r.bottomLeft());
            shape.lineTo(r.left(), r.top() + 5);
            shape.quadTo(r.topLeft(), QPointF(r.left() + 5, r.top()));
            shape.lineTo(r.right() - 9, r.top());
            shape.quadTo(r.topRight(), QPointF(r.right(), r.top() + 9));
            shape.lineTo(r.bottomRight());
            shape.closeSubpath();
            p.setPen(Qt::NoPen);
            p.setBrush(ghost ? QColor(255, 255, 255, on ? 34 : 18) : on ? Theme::surface : Theme::on);
            p.drawPath(shape);
        }
        QColor iconColour = tabs[i].state == State::Connected ? Theme::ok
                            : tabs[i].state == State::Error ? Theme::critical : Theme::idle;
        Icons::paint(p, "terminal", QRectF(pl.tab.left() + 8, (height() - 13) / 2.0, 13, 13), iconColour);
        QFont f = tabFont(font(), on);
        p.setFont(f);
        p.setPen(on ? Theme::text : Theme::faint);
        QRectF titleBox(pl.tab.left() + 8 + 13 + 5, 0, pl.close.left() - 5 - (pl.tab.left() + 8 + 13 + 5), height() - 1);
        p.drawText(titleBox, Qt::AlignVCenter | Qt::AlignLeft,
                QFontMetricsF(f).elidedText(tabs[i].title, Qt::ElideRight, titleBox.width()));
        bool overClose = pl.close.contains(mouse);
        if (overClose) {
            p.setPen(Qt::NoPen);
            p.setBrush(Theme::on.lighter(130));
            p.drawRoundedRect(pl.close, 5, 5);
        }
        Icons::paint(p, "close", pl.close.adjusted(4.5, 4.5, -4.5, -4.5), overClose ? Theme::text : Theme::faint);
    }
    if (plus.contains(mouse)) {
        p.setPen(Qt::NoPen);
        p.setBrush(Theme::on);
        p.drawRoundedRect(plus, 5, 5);
    }
    Icons::paint(p, "plus", QRectF(plus.center().x() - 6.5, plus.center().y() - 6.5, 13, 13), Theme::faint);
}

void TabStrip::mouseMoveEvent(QMouseEvent *e) {
    mouse = e->pos();
    update();
}

void TabStrip::leaveEvent(QEvent *) {
    mouse = {-1, -1};
    update();
}

void TabStrip::mousePressEvent(QMouseEvent *e) {
    QRectF plus;
    QList<Place> places = layoutTabs(&plus);
    if (plus.contains(e->pos())) { emit plusClicked(); return; }
    for (int i = 0; i < places.size(); i++) {
        bool closing = places[i].close.contains(e->pos()) || (e->button() == Qt::MiddleButton && places[i].tab.contains(e->pos()));
        if (closing) { emit closeRequested(i); return; }
        if (places[i].tab.contains(e->pos())) {
            active = i;
            update();
            emit currentChanged(i);
            return;
        }
    }
}
