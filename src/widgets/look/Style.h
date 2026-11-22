#pragma once

#include <QProxyStyle>

/**
 * Fusion with the app's own check boxes and radio buttons: Fusion draws them in the window's colours, which on the
 * dark theme left an empty box all but invisible and a ticked one as a bare check mark.
 */
class Style : public QProxyStyle {
public:
    Style();
    void drawPrimitive(PrimitiveElement element, const QStyleOption *option, QPainter *painter, const QWidget *widget) const override;
};
