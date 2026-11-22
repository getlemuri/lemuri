#include "widgets/Chrome.h"

#include "widgets/Icons.h"
#include "widgets/Theme.h"

#include <QEvent>
#include <QHBoxLayout>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QWindow>

IconButton::IconButton(const QString &icon, int iconSide, QSize size, const QString &tip, QWidget *parent)
    : QAbstractButton(parent), iconName(icon), side(iconSide), fixed(size) {
    setFixedSize(size);
    setToolTip(tip);
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
}

void IconButton::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    bool hover = underMouse() && isEnabled();
    if (activity) {
        if (isChecked()) p.fillRect(0, 0, 2, height(), Theme::activityActive);
    } else if (isChecked() && checkedBackground.alpha()) {
        p.setPen(Qt::NoPen);
        p.setBrush(checkedBackground);
        p.drawRoundedRect(rect(), radius, radius);
    } else if (hover) {
        p.setPen(Qt::NoPen);
        p.setBrush(hoverBackground);
        p.drawRoundedRect(rect(), radius, radius);
    }
    QColor c = (hover || (activity && isChecked())) ? hoverColour : colour;
    if (!isEnabled()) c.setAlphaF(0.35);
    double s = side * (iconName == "ghost" ? 1.2 : 1.0);
    QRectF box((width() - s) / 2, (height() - s) / 2, s, s);
    Icons::paint(p, iconName, box, c, stroke);
    if (badge > 0) {
        QFont f = font();
        f.setPixelSize(10);
        f.setBold(true);
        QString n = QString::number(badge);
        double w = std::max(18.0, QFontMetricsF(f).horizontalAdvance(n) + 10);
        QRectF disc(width() - w - 4, height() - 22, w, 18);
        // A ring of the colour of the bar sets the number apart from the icon under it.
        p.setPen(QPen(Theme::activityBg, 2));
        p.setBrush(Theme::badge);
        p.drawRoundedRect(disc, 9, 9);
        p.setFont(f);
        p.setPen(Qt::white);
        p.drawText(disc, Qt::AlignCenter, n);
    }
}

// ── Top bar ──

TitleBar::TitleBar(QWidget *window) : QWidget(window), window(window) {
    setFixedHeight(38);
    auto *row = new QHBoxLayout(this);
    row->setContentsMargins(0, 0, 10, 0);
    row->setSpacing(6);

    auto *logo = new QLabel;
    logo->setFixedSize(48, 38);
    logo->setAlignment(Qt::AlignCenter);
    QPixmap pm(":/logo.png");
    qreal ratio = devicePixelRatioF();
    pm = pm.scaled(QSize(26, 26) * ratio, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    pm.setDevicePixelRatio(ratio);
    logo->setPixmap(pm);
    row->addWidget(logo);

    auto *name = new QLabel("Lemuri");
    name->setStyleSheet(Theme::css("color: @topText; font-size: 13px; font-weight: bold;"));
    row->addWidget(name);
    version = new QLabel("v0.1");
    version->setStyleSheet(Theme::css("color: @topFaint; background: rgba(255,255,255,0.10); border-radius: 3px; padding: 1px 4px; font-size: 11px;"));
    row->addWidget(version, 0, Qt::AlignVCenter);
    newer = new QPushButton("!");
    newer->setFixedSize(18, 18);
    newer->setCursor(Qt::PointingHandCursor);
    newer->setStyleSheet(Theme::css("QPushButton { background: @amber; border: none; border-radius: 9px; color: @bg; font-weight: bold;"
                         " font-size: 12px; padding: 0; min-height: 18px; max-height: 18px; }"
                         "QPushButton:hover { background: @warn; }"));
    newer->hide();
    connect(newer, &QPushButton::clicked, this, &TitleBar::updateClicked);
    row->addWidget(newer, 0, Qt::AlignVCenter);
    row->addStretch(1);

    auto control = [this, row](const QString &icon) {
        auto *b = new IconButton(icon, 14, QSize(40, 30), QString(), this);
        b->colour = Theme::topFaint;
        b->hoverColour = Theme::topFaint;
        b->hoverBackground = QColor(255, 255, 255, 26);
        b->radius = 8;
        b->stroke = 1.8;
        row->addWidget(b);
        return b;
    };
    connect(control("minimize"), &QAbstractButton::clicked, window, &QWidget::showMinimized);
    maximize = control("maximize");
    connect(maximize, &QAbstractButton::clicked, this, [this] {
        this->window->isMaximized() ? this->window->showNormal() : this->window->showMaximized();
    });
    connect(control("close"), &QAbstractButton::clicked, window, &QWidget::close);
    window->installEventFilter(this);
}

void TitleBar::setVersion(const QString &v) {
    version->setText(v);
}

void TitleBar::setUpdate(const QString &tip) {
    newer->setVisible(!tip.isEmpty());
    newer->setToolTip(tip);
}

bool TitleBar::eventFilter(QObject *o, QEvent *e) {
    if (o == window && e->type() == QEvent::WindowStateChange) maximize->setIconName(window->isMaximized() ? "restore" : "maximize");
    return false;
}

void TitleBar::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.fillRect(rect(), Theme::activityBg);
}

void TitleBar::mousePressEvent(QMouseEvent *e) {
    // The compositor moves the window: the only way that works on Wayland as well.
    if (e->button() == Qt::LeftButton) window->windowHandle()->startSystemMove();
}

void TitleBar::mouseDoubleClickEvent(QMouseEvent *) {
    window->isMaximized() ? window->showNormal() : window->showMaximized();
}

// ── Panels ──

Panel::Panel(const QColor &background, QWidget *parent) : QWidget(parent), background(background) {
    setAttribute(Qt::WA_StyledBackground, false);
}

void Panel::paintEvent(QPaintEvent *) {
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(Qt::NoPen);
    p.setBrush(background);
    p.drawRoundedRect(rect(), Theme::RADIUS, Theme::RADIUS);
}

void Panel::resizeEvent(QResizeEvent *) {
    // The children (a black terminal) are cut to the same corners.
    QPainterPath path;
    path.addRoundedRect(rect(), Theme::RADIUS, Theme::RADIUS);
    setMask(QRegion(path.toFillPolygon().toPolygon()));
}
