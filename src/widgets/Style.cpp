#include "widgets/Style.h"

#include "widgets/Theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QStyleFactory>
#include <QStyleOption>

Style::Style() : QProxyStyle(QStyleFactory::create("Fusion")) {}

void Style::drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const {
    if (element != PE_IndicatorCheckBox && element != PE_IndicatorRadioButton) {
        QProxyStyle::drawPrimitive(element, option, painter, widget);
        return;
    }
    bool on = option->state & (State_On | State_NoChange);
    bool enabled = option->state & State_Enabled;
    bool hover = enabled && (option->state & State_MouseOver);
    QRectF r = QRectF(option->rect).adjusted(1.5, 1.5, -1.5, -1.5);
    double side = std::min(r.width(), r.height());
    r = QRectF(r.center().x() - side / 2, r.center().y() - side / 2, side, side);

    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    if (!enabled) painter->setOpacity(0.45);
    QColor edge = on ? Theme::accent : hover ? Theme::checkEdgeHover : Theme::checkEdge;
    painter->setPen(QPen(edge, 1.2));
    if (element == PE_IndicatorRadioButton) {
        painter->setBrush(Theme::control);
        painter->drawEllipse(r);
        if (on) {
            painter->setPen(Qt::NoPen);
            painter->setBrush(Theme::accent);
            double d = side * 0.5;
            painter->drawEllipse(QRectF(r.center().x() - d / 2, r.center().y() - d / 2, d, d));
        }
    } else {
        painter->setBrush(on ? Theme::accent : Theme::control);
        painter->drawRoundedRect(r, 3, 3);
        if (option->state & State_NoChange) {
            painter->setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap));
            painter->drawLine(QPointF(r.left() + side * 0.28, r.center().y()), QPointF(r.right() - side * 0.28, r.center().y()));
        } else if (on) {
            QPainterPath tick;
            tick.moveTo(r.left() + side * 0.24, r.top() + side * 0.52);
            tick.lineTo(r.left() + side * 0.43, r.top() + side * 0.70);
            tick.lineTo(r.left() + side * 0.77, r.top() + side * 0.32);
            painter->setPen(QPen(Qt::white, 1.8, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
            painter->setBrush(Qt::NoBrush);
            painter->drawPath(tick);
        }
    }
    painter->restore();
}
