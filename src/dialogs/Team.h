#pragma once

#include <QJsonObject>
#include <QWidget>

class Actions;
class Model;
class QTableWidget;
class QLabel;
class QVBoxLayout;

/**
 * Sharing with contacts: sending a host, the envelope to pass on by hand,
 * pasting one, accepting or rejecting what arrives, the contacts and their key cards, pairing over the local network.
 */
namespace TeamDialogs {
void send(Model *model, Actions *actions, QWidget *parent, const QString &hostId);
void paste(Model *model, Actions *actions, QWidget *parent);
void myKey(Model *model, Actions *actions, QWidget *parent);
void addContact(Model *model, Actions *actions, QWidget *parent);
/** `justArrived`: opened by itself on arrival, and then Cancel becomes Later. */
void accept(Model *model, Actions *actions, QWidget *parent, const QJsonObject &delivery, bool justArrived);
void reject(Model *model, QWidget *parent, const QJsonObject &delivery);
void pair(Model *model, QWidget *parent, const QJsonObject &peer);
/** Another PC asks to pair: the contact's name if the user accepts, empty if not. */
QString pairingRequest(Model *model, QWidget *parent, const QJsonObject &peer, const QString &code);
void addAddress(Model *model, QWidget *parent);
void waitForLink(Model *model, QWidget *parent);
}  // namespace TeamDialogs

/**
 * The Team page: this user's key, the hosts received, the contacts and the PCs of the local network.
 * While the vault is locked it shows only a sign-in with the master password (or, with no vault yet, the choice of
 * one); the rest appears once it is open.
 */
class TeamView : public QWidget {
    Q_OBJECT
public:
    TeamView(Model *model, Actions *actions, QWidget *parent = nullptr);

protected:
    void resizeEvent(class QResizeEvent *) override;

private:
    void fill();
    QWidget *signIn();
    void unlock();

    Model *model;
    Actions *actions;
    class QStackedWidget *stack;
    QWidget *content;
    // The sign-in
    QWidget *login;
    QLabel *loginTitle, *loginText, *loginNote, *loginError;
    class QLineEdit *password, *again;
    class QPushButton *loginButton;
    bool asking = false;

    QLabel *stateChip, *message, *fingerprint, *incomingTitle, *lanMessage, *firewall, *peerWarnings;
    class QCheckBox *lanOn;
    class QPushButton *byAddress, *waitLink;
    QTableWidget *incoming, *contacts, *nearby;
    QWidget *incomingPanel;
    class QBoxLayout *columns;
};
