#pragma once

#include <QColor>
#include <QIcon>
#include <QPainterPath>
#include <QPixmap>
#include <QString>

class QLabel;
class QPainter;

/** The app's stroke icons: SVG path data on a 24 x 24 grid, drawn with round caps and joins. */
namespace Icons {
/** The path of an icon, on its 24 x 24 grid. */
QPainterPath path(const QString &name);
/** Draws an icon in the square `box`, with a stroke of 2 on the grid, unless given. */
void paint(QPainter &p, const QString &name, const QRectF &box, const QColor &colour, double stroke = 2);
QPixmap pixmap(const QString &name, int side, const QColor &colour, double stroke = 2, qreal ratio = 1);
/** An icon drawn when shown: in one of the theme's colours, it follows a change of theme. */
QIcon icon(const QString &name, int side, const QColor &colour, double stroke = 2);
/** The icon as the label's picture, drawn again at a change of theme. */
void show(QLabel *label, const QString &name, int side, const QColor &colour, double stroke = 2);
}  // namespace Icons
