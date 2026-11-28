#pragma once

#include <QAbstractButton>
#include <QColor>
#include <QWidget>

#include "widgets/look/Theme.h"

/**
 * A button that is only an icon: a rounded patch under the mouse, the icon a little
 * brighter; checkable ones keep a patch (or, for the activity bar, a white line on the left) while on.
 */
class IconButton : public QAbstractButton {
    Q_OBJECT
public:
    IconButton(const QString &icon, int iconSide, QSize size, const QString &tip, QWidget *parent = nullptr);

    void setIconName(const QString &name) { iconName = name; update(); }
    Theme::Colour colour = QColor("#a0a0a0");
    Theme::Colour hoverColour = QColor("#e6e6e6");
    Theme::Colour hoverBackground = Theme::hover;
    Theme::Colour checkedBackground = Qt::transparent;
    int radius = 5;
    double stroke = 2;
    /** The activity bar's look: no patch, a 2 px white line on the left when checked. */
    bool activity = false;
    /** A number in a small blue disc at the bottom right (the activity bar's arrivals); 0: none. */
    int badge = 0;

    QSize sizeHint() const override { return fixed; }

protected:
    void paintEvent(QPaintEvent *) override;
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }

private:
    QString iconName;
    int side;
    QSize fixed;
};

/** The top bar: the app icon in a box as wide as the activity bar, name and version, the window controls. */
class TitleBar : public QWidget {
    Q_OBJECT
public:
    explicit TitleBar(QWidget *window);
    void setVersion(const QString &v);
    /** The yellow "!" next to the version while a newer one is out; empty tip hides it. */
    void setUpdate(const QString &tip);

signals:
    void updateClicked();

protected:
    void mousePressEvent(QMouseEvent *) override;
    void mouseDoubleClickEvent(QMouseEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;

private:
    QWidget *window;
    IconButton *maximize;
    class QLabel *version;
    class QPushButton *newer;
};

/** A panel with slightly rounded corners, on the grey of the gaps between panels. */
class Panel : public QWidget {
    Q_OBJECT
public:
    Panel(const QColor &background, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    Theme::Colour background;
};
