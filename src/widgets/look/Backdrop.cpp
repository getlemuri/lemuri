#include "widgets/look/Backdrop.h"

#include "widgets/look/Theme.h"

#include <QHash>
#include <QImage>
#include <QLinearGradient>
#include <QPainter>
#include <QtMath>

namespace {
/** One wave: the area under a sine curve that climbs from left to right, lit along its crest and fading downwards. */
struct Wave {
    double left, right;  // Height of the curve's middle at the two sides, as a share of the height.
    double amplitude;    // As a share of the height.
    double periods;      // Whole waves across the width.
    double phase;
    int glow;            // Alpha of the colour at the crest.
};

const Wave WAVES[] = {
    {0.78, 0.30, 0.060, 0.9, 0.4, 34},
    {0.92, 0.52, 0.045, 1.3, 2.1, 26},
    {1.02, 0.72, 0.035, 1.7, 4.0, 20},
};

QImage render(const QSize &size) {
    QImage img(size, QImage::Format_RGB32);
    QPainter p(&img);
    p.setRenderHint(QPainter::Antialiasing);
    const double w = size.width(), h = size.height();

    QLinearGradient base(0, 0, w, h);
    base.setColorAt(0, Theme::backdropFrom);
    base.setColorAt(0.55, Theme::backdropMiddle);
    base.setColorAt(1, Theme::backdropTo);
    p.fillRect(img.rect(), base);

    for (const Wave &v : WAVES) {
        QPainterPath crest;
        double top = h;
        for (double x = 0; x <= w + 8; x += 8) {
            double t = std::min(x, w) / std::max(w, 1.0);
            double y = h * (v.left + (v.right - v.left) * t) + v.amplitude * h * std::sin(2 * M_PI * v.periods * t + v.phase);
            top = std::min(top, y);
            if (x == 0) crest.moveTo(0, y);
            else crest.lineTo(std::min(x, w), y);
        }
        QPainterPath body = crest;
        body.lineTo(w, h);
        body.lineTo(0, h);
        body.closeSubpath();
        // Bright just under the crest, gone a third of the height lower.
        QLinearGradient fade(0, top, 0, top + h * 0.35);
        QColor c = Theme::backdropWave;
        c.setAlpha(v.glow);
        fade.setColorAt(0, c);
        c.setAlpha(0);
        fade.setColorAt(1, c);
        p.fillPath(body, fade);
        // A faint line along the crest draws the wave.
        QColor line = Theme::backdropCrest;
        line.setAlpha(v.glow / 2);
        p.strokePath(crest, QPen(line, 1.2));
    }
    return img;
}
}  // namespace

void Backdrop::paint(QPainter &p, const QPainterPath &shape, const QRectF &area) {
    // A few sizes are kept: the main window, a dialog or two; a resize makes new ones.
    static QHash<quint64, QImage> made;
    QSize size = area.size().toSize().expandedTo(QSize(1, 1));
    quint64 key = (quint64(size.width()) << 32) | quint64(size.height());
    auto it = made.constFind(key);
    if (it == made.constEnd()) {
        if (made.size() >= 6) made.clear();
        it = made.insert(key, render(size));
    }
    QBrush brush(*it);
    brush.setTransform(QTransform::fromTranslate(area.x(), area.y()));
    p.fillPath(shape, brush);
}
