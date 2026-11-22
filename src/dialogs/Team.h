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

/** The Team page: this user's key, the hosts received, the contacts and the PCs of the local network. */
class TeamView : public QWidget {
    Q_OBJECT
public:
    TeamView(Model *model, Actions *actions, QWidget *parent = nullptr);

private:
    void fill();

    Model *model;
    Actions *actions;
    QLabel *stateChip, *message, *fingerprint, *incomingTitle, *lanMessage, *firewall, *peerWarnings;
    QWidget *openVault;
    class QCheckBox *lanOn;
    class QPushButton *byAddress, *waitLink;
    QTableWidget *incoming, *contacts, *nearby;
};
