#pragma once

#include <QElapsedTimer>
#include <QImage>
#include <QTimer>
#include <QWidget>
#include <functional>

/**
 * The splash screen: a water vortex that sucks everything towards the centre, out of which the
 * logo (the lemur behind the #) unrolls, then the Lemuri caption. It lasts about three seconds; a click skips it.
 * The main window opens when it ends.
 */
class Splash : public QWidget {
public:
    /** Shows the screen; when it ends (or is skipped with a click) it calls `after` and goes. */
    explicit Splash(std::function<void()> after);

    /**
     * Whether to show it: the "splash" setting (ui node), on unless turned off. The backend gives the settings only
     * once started, so the last known choice is kept in the app's cache.
     */
    static bool wanted();
    static void remember(bool on);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    void tick();
    void finish();
    void arms(QPainter &g, double c, double rotation, double shrink, double alpha);
    void drops(QPainter &g, double c, double t, double shrink, double alpha);
    void waves(QPainter &g, double c, double t);
    void hash(QPainter &g, double c, double t);
    void caption(QPainter &g, double c, double t);

    std::function<void()> after;
    QTimer frames;
    QElapsedTimer clock;
    qint64 lost = 0, last = -1;
    double t = 0;
    bool finished = false;
    QImage logo, glow;
    QList<double> corner, radius, speed;
};
