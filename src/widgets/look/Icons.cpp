#include "widgets/look/Icons.h"

#include "widgets/look/IconTraces.h"
#include "widgets/look/Theme.h"

#include <QGuiApplication>
#include <QIconEngine>
#include <QLabel>
#include <QPainter>
#include <QtMath>
#include <cmath>

namespace {

/** Reads SVG path data (M L H V C S Q T A Z, absolute and relative) into a QPainterPath. */
class PathReader {
public:
    explicit PathReader(const QString &d) : s(d) {}

    QPainterPath read() {
        QChar command;
        while (skip(), i < s.size()) {
            if (s[i].isLetter()) command = s[i++];
            else if (command.isNull()) break;
            step(command);
            // After a moveto, more pairs of numbers are lineto.
            if (command == 'M') command = 'L';
            else if (command == 'm') command = 'l';
        }
        return path;
    }

private:
    void skip() {
        while (i < s.size() && (s[i].isSpace() || s[i] == ',')) i++;
    }

    double number() {
        skip();
        int start = i;
        if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++;
        bool dot = false, exp = false;
        while (i < s.size()) {
            QChar c = s[i];
            if (c.isDigit()) i++;
            else if (c == '.' && !dot && !exp) { dot = true; i++; }
            else if ((c == 'e' || c == 'E') && !exp) { exp = true; i++; if (i < s.size() && (s[i] == '-' || s[i] == '+')) i++; }
            else break;
        }
        return s.mid(start, i - start).toDouble();
    }

    /** A pair of numbers, x then y (read one after the other: the order of a constructor's arguments is not fixed). */
    QPointF point() {
        double x = number();
        double y = number();
        return {x, y};
    }

    /** One arc flag: a single 0 or 1, which SVG lets be written with no separator. */
    bool flag() {
        skip();
        return s[i++] == '1';
    }

    void step(QChar c) {
        bool rel = c.isLower();
        QPointF base = rel ? cur : QPointF();
        switch (c.toUpper().unicode()) {
            case 'M': cur = base + point(); start = cur; path.moveTo(cur); lastCtrl = cur; break;
            case 'L': cur = base + point(); line(cur); break;
            case 'H': cur.setX((rel ? cur.x() : 0) + number()); line(cur); break;
            case 'V': cur.setY((rel ? cur.y() : 0) + number()); line(cur); break;
            case 'C': {
                QPointF c1 = base + point(), c2 = base + point();
                cur = base + point();
                path.cubicTo(c1, c2, cur);
                lastCtrl = c2;
                return;
            }
            case 'S': {
                QPointF c1 = 2 * cur - lastCtrl, c2 = base + point();
                cur = base + point();
                path.cubicTo(c1, c2, cur);
                lastCtrl = c2;
                return;
            }
            case 'Q': {
                QPointF c1 = base + point();
                cur = base + point();
                path.quadTo(c1, cur);
                lastCtrl = c1;
                return;
            }
            case 'T': {
                QPointF c1 = 2 * cur - lastCtrl;
                cur = base + point();
                path.quadTo(c1, cur);
                lastCtrl = c1;
                return;
            }
            case 'A': {
                double rx = number(), ry = number(), rot = number();
                bool large = flag(), sweep = flag();
                QPointF to = base + point();
                arc(rx, ry, rot, large, sweep, to);
                cur = to;
                break;
            }
            case 'Z': path.closeSubpath(); cur = start; break;
        }
        lastCtrl = cur;
    }

    /** A segment; a zero-length one (a dot, "h0") is made a hair long, so the round cap draws it. */
    void line(QPointF to) {
        if (path.currentPosition() == to) to += QPointF(0.01, 0);
        path.lineTo(to);
    }

    /** SVG's endpoint arc, through its centre form (SVG 1.1, appendix F.6.5), drawn as short lines. */
    void arc(double rx, double ry, double rotDeg, bool large, bool sweep, QPointF to) {
        QPointF from = cur;
        if (rx == 0 || ry == 0 || from == to) { line(to); return; }
        rx = std::abs(rx);
        ry = std::abs(ry);
        double phi = qDegreesToRadians(rotDeg), cp = std::cos(phi), sp = std::sin(phi);
        double dx = (from.x() - to.x()) / 2, dy = (from.y() - to.y()) / 2;
        double x1 = cp * dx + sp * dy, y1 = -sp * dx + cp * dy;
        double l = x1 * x1 / (rx * rx) + y1 * y1 / (ry * ry);
        if (l > 1) { rx *= std::sqrt(l); ry *= std::sqrt(l); }
        double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
        double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
        double k = std::sqrt(std::max(0.0, num / den)) * (large == sweep ? -1 : 1);
        double cx1 = k * rx * y1 / ry, cy1 = -k * ry * x1 / rx;
        double cx = cp * cx1 - sp * cy1 + (from.x() + to.x()) / 2, cy = sp * cx1 + cp * cy1 + (from.y() + to.y()) / 2;
        auto angle = [](double ux, double uy, double vx, double vy) {
            return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy);
        };
        double t1 = angle(1, 0, (x1 - cx1) / rx, (y1 - cy1) / ry);
        double dt = angle((x1 - cx1) / rx, (y1 - cy1) / ry, (-x1 - cx1) / rx, (-y1 - cy1) / ry);
        if (!sweep && dt > 0) dt -= 2 * M_PI;
        else if (sweep && dt < 0) dt += 2 * M_PI;
        int n = std::max(4, int(std::ceil(std::abs(dt) / (M_PI / 24))));
        for (int j = 1; j <= n; j++) {
            double t = t1 + dt * j / n;
            path.lineTo(cx + rx * std::cos(t) * cp - ry * std::sin(t) * sp, cy + rx * std::cos(t) * sp + ry * std::sin(t) * cp);
        }
    }

    QString s;
    int i = 0;
    QPainterPath path;
    QPointF cur, start, lastCtrl;
};

}  // namespace

QPainterPath Icons::path(const QString &name) {
    static QHash<QString, QPainterPath> cache;
    auto it = cache.find(name);
    if (it != cache.end()) return *it;
    QPainterPath p = PathReader(iconTraces().value(name)).read();
    cache.insert(name, p);
    return p;
}

void Icons::paint(QPainter &p, const QString &name, const QRectF &box, const QColor &colour, double stroke) {
    p.save();
    p.setRenderHint(QPainter::Antialiasing);
    p.translate(box.topLeft());
    p.scale(box.width() / 24.0, box.height() / 24.0);
    QPen pen(colour, stroke, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
    p.setPen(pen);
    p.setBrush(Qt::NoBrush);
    p.drawPath(path(name));
    p.restore();
}

QPixmap Icons::pixmap(const QString &name, int side, const QColor &colour, double stroke, qreal ratio) {
    QPixmap pm(QSize(side, side) * ratio);
    pm.setDevicePixelRatio(ratio);
    pm.fill(Qt::transparent);
    QPainter p(&pm);
    paint(p, name, QRectF(0, 0, side, side), colour, stroke);
    return pm;
}

namespace {
/** Draws the icon each time it is asked for, so in the theme's colour of the moment. */
class Engine : public QIconEngine {
public:
    Engine(const QString &name, int side, const QColor &colour, double stroke) : name(name), side(side), colour(colour), stroke(stroke) {}
    void paint(QPainter *p, const QRect &rect, QIcon::Mode mode, QIcon::State) override {
        QColor c = colour;
        if (mode == QIcon::Disabled) c.setAlphaF(c.alphaF() * 0.35);
        double s = std::min(rect.width(), rect.height());
        Icons::paint(*p, name, QRectF(rect.x() + (rect.width() - s) / 2, rect.y() + (rect.height() - s) / 2, s, s), c, stroke);
    }
    QPixmap pixmap(const QSize &size, QIcon::Mode mode, QIcon::State state) override { return scaledPixmap(size, mode, state, 1); }
    QPixmap scaledPixmap(const QSize &size, QIcon::Mode mode, QIcon::State state, qreal scale) override {
        QPixmap pm(size * scale);
        pm.setDevicePixelRatio(scale);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        paint(&p, QRect(QPoint(0, 0), size), mode, state);
        return pm;
    }
    QSize actualSize(const QSize &size, QIcon::Mode, QIcon::State) override { return size.boundedTo(QSize(side, side)); }
    QList<QSize> availableSizes(QIcon::Mode, QIcon::State) override { return {QSize(side, side)}; }
    QIconEngine *clone() const override { return new Engine(*this); }

private:
    QString name;
    int side;
    Theme::Colour colour;
    double stroke;
};
}  // namespace

QIcon Icons::icon(const QString &name, int side, const QColor &colour, double stroke) {
    return QIcon(new Engine(name, side, colour, stroke));
}

void Icons::show(QLabel *label, const QString &name, int side, const QColor &colour, double stroke) {
    Theme::Colour c = colour;
    auto draw = [label, name, side, c, stroke] { label->setPixmap(pixmap(name, side, c, stroke, label->devicePixelRatioF())); };
    draw();
    if (Theme::named(&colour)) Theme::follow(label, draw);
}
