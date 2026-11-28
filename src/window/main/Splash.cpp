#include "window/main/Splash.h"

#include "app/launch/Instance.h"
#include "app/tools/Texts.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGraphicsDropShadowEffect>
#include <QGraphicsPixmapItem>
#include <QGraphicsScene>
#include <QGuiApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QRadialGradient>
#include <QRandomGenerator>
#include <QScreen>
#include <QStandardPaths>
#include <QtMath>
#include <algorithm>
#include <cmath>

namespace {
constexpr int SIDE = 440;
constexpr double DURATION = 3.0;
/** Everything fades in the last half second. */
constexpr double FADE = 0.5;
constexpr int DROPS = 140;
/** The side of the logo once it has come out. */
constexpr int LOGO = 230;
/** A longer gap between two frames means the app was busy: the animation does not jump ahead. */
constexpr qint64 STALL_MS = 100;

double closed(double x) { return std::clamp(x, 0.0, 1.0); }

/** Smooth curve from 0 to 1 (smoothstep). */
double smooth(double x) {
    x = closed(x);
    return x * x * (3 - 2 * x);
}

/** From 0 to 1, overshooting a little and coming back. */
double bounce(double x) {
    double k = 1.70158;
    x = x - 1;
    return 1 + x * x * ((k + 1) * x + k);
}

QColor colour(const char *hex, double alpha) {
    QColor c(hex);
    c.setAlphaF(float(closed(alpha)));
    return c;
}

QString startupFile() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/startup.json";
}

/** `image` with a soft light around it, drawn once, then only moved. */
QImage withGlow(const QImage &image, const QColor &light, double blur, int pad) {
    QGraphicsScene scene;
    auto *item = new QGraphicsPixmapItem(QPixmap::fromImage(image));
    auto *shadow = new QGraphicsDropShadowEffect;
    shadow->setBlurRadius(blur);
    shadow->setColor(light);
    shadow->setOffset(0, 0);
    item->setGraphicsEffect(shadow);
    scene.addItem(item);
    QRectF area(-pad, -pad, image.width() + 2 * pad, image.height() + 2 * pad);
    scene.setSceneRect(area);
    QImage out(area.size().toSize(), QImage::Format_ARGB32_Premultiplied);
    out.fill(Qt::transparent);
    QPainter p(&out);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    scene.render(&p, QRectF(QPointF(0, 0), area.size()), area);
    return out;
}
}  // namespace

bool Splash::wanted() {
    QFile f(startupFile());
    if (!f.open(QIODevice::ReadOnly)) return true;
    return QJsonDocument::fromJson(f.readAll()).object()["splash"].toBool(true);
}

void Splash::remember(bool on) {
    if (wanted() == on) return;
    QDir().mkpath(QFileInfo(startupFile()).path());
    QFile f(startupFile());
    // The file keeps the theme too (Theme::remember): only this choice changes.
    QJsonObject o;
    if (f.open(QIODevice::ReadOnly)) o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    o["splash"] = on;
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

Splash::Splash(std::function<void()> after) : after(std::move(after)) {
    setWindowFlags(Qt::SplashScreen | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose);
    setWindowTitle("Lemuri");
    setFixedSize(SIDE, SIDE);
    QScreen *s = QGuiApplication::primaryScreen();
    QRect area = s ? Instance::workArea(s) : QRect(0, 0, SIDE, SIDE);
    move(area.center() - QPoint(SIDE / 2, SIDE / 2));

    // The 512 px logo shrunk once, smoothly, for the screen's pixels: shrunk at every frame the fur turns into noise.
    double dpr = s ? s->devicePixelRatio() : 1;
    int px = int(std::lround(LOGO * dpr));
    logo = QImage(":/logo.png").scaled(px, px, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    logo.setDevicePixelRatio(1);
    glow = withGlow(logo, QColor("#3fbf8a"), 36 * dpr, int(24 * dpr));
    glow.setDevicePixelRatio(dpr);

    QRandomGenerator random(7);
    for (int i = 0; i < DROPS; i++) {
        corner << random.generateDouble() * M_PI * 2;
        radius << 40 + random.generateDouble() * 170;
        speed << 0.6 + random.generateDouble() * 0.8;
    }
    connect(&frames, &QTimer::timeout, this, &Splash::tick);
    frames.setTimerType(Qt::PreciseTimer);
    frames.start(16);
    clock.start();
    show();
}

void Splash::tick() {
    qint64 now = clock.elapsed();
    if (last >= 0 && now - last > STALL_MS) lost += now - last;
    last = now;
    t = (now - lost) / 1000.0;
    update();
    if (t >= DURATION) finish();
}

void Splash::mousePressEvent(QMouseEvent *) {
    finish();
}

void Splash::finish() {
    if (finished) return;
    finished = true;
    frames.stop();
    // First the main window, then this one goes.
    if (after) after();
    close();
}

// ── Drawing ──────────────────────────────────────────────────────────────

void Splash::paintEvent(QPaintEvent *) {
    QPainter g(this);
    g.setRenderHint(QPainter::Antialiasing);
    g.setRenderHint(QPainter::SmoothPixmapTransform);
    double c = SIDE / 2.0;
    g.setCompositionMode(QPainter::CompositionMode_Source);
    g.fillRect(rect(), Qt::transparent);
    g.setCompositionMode(QPainter::CompositionMode_SourceOver);
    g.setOpacity(1 - closed((t - (DURATION - FADE)) / FADE));

    // The black disc: solid only in the middle, then fading slowly into transparency, with no visible edge.
    QRadialGradient disc(c, c, SIDE / 2.0);
    disc.setColorAt(0, QColor("#161616"));
    disc.setColorAt(0.3, QColor("#0a0a0a"));
    disc.setColorAt(0.5, colour("#000000", 0.95));
    disc.setColorAt(0.65, colour("#000000", 0.8));
    disc.setColorAt(0.78, colour("#000000", 0.5));
    disc.setColorAt(0.88, colour("#000000", 0.22));
    disc.setColorAt(0.95, colour("#000000", 0.07));
    disc.setColorAt(1, colour("#000000", 0));
    g.setPen(Qt::NoPen);
    g.setBrush(disc);
    g.drawEllipse(QRectF(0, 0, SIDE, SIDE));

    // The vortex spins harder and harder, then tightens and calms down while the hash comes out.
    double force = std::min(1.0, t / 1.2);
    double shrink = 1 - 0.55 * smooth((t - 0.9) / 0.9);
    double calm = 1 - 0.75 * smooth((t - 1.5) / 0.8);
    double rotation = t * t * 2.2 + t * 1.5;
    arms(g, c, rotation, shrink, force * calm);
    drops(g, c, t, shrink, force * calm);
    waves(g, c, t);

    // The centre lights up when the vortex sucks in.
    double light = smooth((t - 0.6) / 0.6) * (1 - 0.6 * smooth((t - 1.8) / 0.6));
    QRadialGradient centre(c, c, 90);
    centre.setColorAt(0, colour("#c8f0dc", 0.85 * light));
    centre.setColorAt(1, colour("#3fbf8a", 0));
    g.setPen(Qt::NoPen);
    g.setBrush(centre);
    g.drawEllipse(QRectF(c - 90, c - 90, 180, 180));

    hash(g, c, t);
    caption(g, c, t);
}

/** Logarithmic spiral arms, lighter towards the centre. */
void Splash::arms(QPainter &g, double c, double rotation, double shrink, double alpha) {
    const int count = 6;
    for (int b = 0; b < count; b++) {
        double phase = rotation + b * M_PI * 2 / count;
        QPointF prev(c, c);
        for (int i = 1; i <= 60; i++) {
            double s = i / 60.0;
            double r = (14 + 196 * s * s) * shrink;
            double a = phase - s * 4.2;
            QPointF p(c + std::cos(a) * r, c + std::sin(a) * r);
            if (i > 1) {
                g.setPen(QPen(colour("#7fdcb0", alpha * 0.55 * (1 - s) + 0.04 * alpha), 1 + 5 * (1 - s), Qt::SolidLine, Qt::RoundCap));
                g.drawLine(prev, p);
            }
            prev = p;
        }
    }
}

/** Drops that spin and get sucked towards the centre; once there, they start again from the edge. */
void Splash::drops(QPainter &g, double c, double t, double shrink, double alpha) {
    g.setPen(Qt::NoPen);
    for (int i = 0; i < DROPS; i++) {
        double r = radius[i] - std::fmod(t * 60 * speed[i], 190);
        if (r < 12) r += 190;
        double a = corner[i] + t * (2.5 + 260 / r) * speed[i];
        double rr = r * shrink;
        double x = c + std::cos(a) * rr, y = c + std::sin(a) * rr;
        double dim = 1 + 2.2 * (r / 210);
        g.setBrush(colour("#c8f0dc", alpha * (0.25 + 0.55 * (1 - r / 210))));
        g.drawEllipse(QRectF(x - dim / 2, y - dim / 2, dim, dim));
    }
}

/** Water rings widening from the centre when the hash comes out. */
void Splash::waves(QPainter &g, double c, double t) {
    g.setBrush(Qt::NoBrush);
    for (int k = 0; k < 3; k++) {
        double s = (t - 1.1 - k * 0.25) / 1.1;
        if (s <= 0 || s >= 1) continue;
        double r = 40 + 170 * s;
        // Faster than they grow: out where the disc fades they would draw its edge again.
        g.setPen(QPen(colour("#7fdcb0", 0.5 * (1 - s) * (1 - s)), 2 * (1 - s) + 0.5));
        g.drawEllipse(QRectF(c - r, c - r, r * 2, r * 2));
    }
}

/** The logo comes out of the vortex spinning backwards and grows with a small bounce. */
void Splash::hash(QPainter &g, double c, double t) {
    double s = (t - 1.0) / 0.8;
    if (s <= 0) return;
    double p = std::min(1.0, s);
    double scale = bounce(p);
    double turn = -540 * std::pow(1 - p, 3);
    double alpha = std::min(1.0, s * 2);
    g.save();
    g.translate(c, c - 40 * smooth((t - 1.6) / 0.6));
    g.rotate(turn);
    g.scale(scale, scale);
    g.setOpacity(g.opacity() * alpha);
    QSizeF size = glow.deviceIndependentSize();
    g.drawImage(QRectF(-size.width() / 2, -size.height() / 2, size.width(), size.height()), glow);
    g.restore();
}

/** The caption rises and appears below the logo. */
void Splash::caption(QPainter &g, double c, double t) {
    double s = smooth((t - 1.6) / 0.6);
    if (s <= 0) return;
    g.save();
    g.setOpacity(g.opacity() * s);
    QFont big = font();
    big.setPixelSize(34);
    big.setBold(true);
    g.setFont(big);
    QRectF line(0, c + 112 + 16 * (1 - s) - 25, SIDE, 50);
    // A soft dark edge under the white letters.
    g.setPen(colour("#000000", 0.35));
    for (QPointF d : {QPointF(-1.5, 0), QPointF(1.5, 0), QPointF(0, -1.5), QPointF(0, 2)}) g.drawText(line.translated(d), Qt::AlignCenter, "Lemuri");
    g.setPen(Qt::white);
    g.drawText(line, Qt::AlignCenter, "Lemuri");
    QFont small = font();
    small.setPixelSize(12);
    g.setFont(small);
    g.setPen(QColor("#a8e6c8"));
    // Before the texts have ever come from the backend there is no tagline: only the name.
    QString tagline = T("app.tagline");
    if (!tagline.startsWith('!')) g.drawText(QRectF(0, c + 140 + 16 * (1 - s) - 12, SIDE, 24), Qt::AlignCenter, tagline);
    g.restore();
}
