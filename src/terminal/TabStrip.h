#pragma once

#include <QList>
#include <QWidget>

/**
 * The row of terminal tabs: 23 px, against the top edge, in the colour of the side column;
 * the active tab in the colour of the surface. Each tab: terminal icon (green while connected), name, close.
 */
class TabStrip : public QWidget {
    Q_OBJECT
public:
    enum class State { Connecting, Connected, Closed, Error };

    explicit TabStrip(QWidget *parent = nullptr);

    struct Item {
        QString title, tip;
        State state = State::Connecting;
    };
    /** All the tabs at once, with the active one; no signal. */
    void setTabs(const QList<Item> &items, int active);
    int add(const QString &title);
    void remove(int i);
    void setCurrent(int i);
    int current() const { return active; }
    int count() const { return int(tabs.size()); }
    void setTitle(int i, const QString &title);
    void setState(int i, State s);

    QSize sizeHint() const override { return {200, 23}; }
    /** Ghost mode: no background, the active tab a light veil. */
    bool ghost = false;

signals:
    void currentChanged(int i);
    void closeRequested(int i);
    void plusClicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;
    bool event(QEvent *) override;

private:
    struct Tab {
        QString title;
        State state = State::Connecting;
        QString tip;
    };
    /** Where each tab is, its close button and the "+" after them, as last laid out. */
    struct Place {
        QRectF tab, close;
    };
    QList<Place> layoutTabs(QRectF *plus) const;

    QList<Tab> tabs;
    int active = -1;
    QPoint mouse{-1, -1};
};
