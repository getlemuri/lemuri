#pragma once

#include <QPainterPath>
#include <QRectF>

class QPainter;

/**
 * The backdrop of the main window and of the dialogs: a diagonal shade from near black (top left) to a dark slate
 * green (bottom right), crossed by soft waves of the same green. Drawn once for each size and kept, so a terminal
 * that repaints over it many times a second only copies it.
 */
namespace Backdrop {
/** Fills `shape` with the backdrop, laid out over `area` (the whole window, in the painter's coordinates). */
void paint(QPainter &p, const QPainterPath &shape, const QRectF &area);
}  // namespace Backdrop
