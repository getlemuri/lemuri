#pragma once

#include "app/core/Model.h"

#include <QDialog>
#include <QLabel>
#include <QPushButton>
#include <QStringList>
#include <QWidget>
#include <functional>
#include <optional>

class IconButton;
class QBoxLayout;
class QDialogButtonBox;
class QJsonObject;
class QLineEdit;
class QVBoxLayout;

/** The pieces the views are made of, in the colours of the Theme. */
namespace Ui {
/** The style sheet of the standard controls: buttons, fields, tables, menus, scroll bars, tooltips. */
QString styleSheet();
/** The scroll bars of the whole app (a rounded thumb, no track), for windows that do not take styleSheet(). */
QString scrollBars();

QPushButton *button(const QString &text, const QString &icon, std::function<void()> action = nullptr);
QPushButton *primary(const QString &text, const QString &icon, std::function<void()> action = nullptr);
QPushButton *danger(const QString &text, const QString &icon, std::function<void()> action = nullptr);
/** A small icon-only command: faint, blue under the mouse. */
IconButton *action(const QString &icon, const QString &tip, std::function<void()> act = nullptr);

/** A coloured badge: tone ok, warn, error, info or neutral. */
QLabel *chip(const QString &tone, const QString &text);
QString chipStyle(const QString &tone);
/** The reachability of a host as a badge. */
QLabel *hostState(Host::Reach r);
QString hostStateText(Host::Reach r);
QString sessionStateText(Session::State s);
QLabel *faint(const QString &text);
QLabel *code(const QString &text);

/** A panel with a head (bold title) and a body. */
QWidget *panel(const QString &title, QWidget *body, QWidget *headExtra = nullptr);
/** A row of buttons on the grey of a toolbar. */
QWidget *bar(const QList<QWidget *> &left, const QList<QWidget *> &right = {});

/** KiB in a readable unit, the way df -h does. */
QString measure(qint64 kb);
QString dateTime(qint64 ms);
/** Every disk of a reading, one per line, with when it was read: the tooltip of the disk bars. */
QString disksTip(const QJsonObject &metrics);
/** The fullest disk of a reading, or an empty object. */
QJsonObject fullestDisk(const QJsonObject &metrics);
double quota(const QJsonObject &disk);
QColor level(double quota);

/** A password field with an eye at its end: a click shows what is typed, another hides it again. */
void revealable(QLineEdit *field);

/** A modal question with OK and Cancel. */
bool confirm(QWidget *parent, const QString &title, const QString &text, const QString &action, bool danger = false);
void warning(QWidget *parent, const QString &title, const QString &text);
/** A name, checked as it is typed (`check` returns why it cannot be used, or empty). */
std::optional<QString> name(QWidget *parent, const QString &title, const QString &label, const QString &start,
                            const QString &action, std::function<QString(const QString &)> check);
/** A folder among `choices` ("" is the Hosts root). */
std::optional<QString> folder(QWidget *parent, const QString &title, const QString &label, const QStringList &choices, const QString &start);
}  // namespace Ui

/** Usage bar with the percentage and a text next to it; colours as the rings. */
class UsageBar : public QWidget {
public:
    UsageBar(double quota, const QString &text, int trackWidth, QWidget *parent = nullptr);
    QSize sizeHint() const override;

protected:
    void paintEvent(QPaintEvent *) override;

private:
    double q;
    QString text;
    int track;
};

/** Usage ring: percentage in the centre, title above, detail below. With `score` more is better. */
class Gauge : public QWidget {
public:
    Gauge(const QString &title, bool score = false, QWidget *parent = nullptr);
    void set(std::optional<double> quota, const QString &detail);
    QSize sizeHint() const override { return {160, 150}; }

protected:
    void paintEvent(QPaintEvent *) override;

private:
    QString title, detail;
    std::optional<double> value;
    bool score;
};

/**
 * A dialog in the app's frame: grey title bar with the app name, the title in the
 * middle and a close button, the body, then the buttons on a darker bar. Moved by its title bar.
 */
class FramedDialog : public QDialog {
    Q_OBJECT
public:
    FramedDialog(QWidget *parent, const QString &title);
    /** Where the content goes. */
    QVBoxLayout *body() const { return content; }
    /** Adds Cancel and the main button; returns the main one. */
    QPushButton *buttons(const QString &ok, bool danger = false, const QString &cancel = {});
    QPushButton *addButton(const QString &text, bool left = false);

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    /** Before it shows: every wrapped text gets the height it needs at its real width (see fitTexts). */
    void showEvent(QShowEvent *) override;
    void resizeEvent(QResizeEvent *) override;

private:
    /**
     * A wrapped QLabel tells the layout its height for a width it guesses, and a top-level window does not ask
     * again at the real width: a text of two lines could get the height of one and a half, cut at the top and the
     * bottom. Each one gets its height for the width it has; the window grows to hold them. Until nothing changes.
     */
    void fitTexts();
    bool fitting = false;

private:
    QVBoxLayout *content;
    QBoxLayout *buttonRow;
    QWidget *foot;
};
