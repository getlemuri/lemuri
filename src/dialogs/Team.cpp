#include "dialogs/Team.h"

#include "app/core/Backend.h"
#include "app/core/Model.h"
#include "app/tools/Texts.h"
#include "widgets/parts/Chrome.h"
#include "widgets/look/Icons.h"
#include "widgets/look/Theme.h"
#include "widgets/parts/Ui.h"
#include "window/main/Actions.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QElapsedTimer>
#include <QPointer>
#include <QTimer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QTableWidget>
#include <QVBoxLayout>
#include <memory>

namespace {
const QString BEGIN = "-----BEGIN LEMURI HOST-----", END = "-----END LEMURI HOST-----";

QLabel *note(const QString &text) {
    QLabel *l = Ui::faint(text);
    l->setWordWrap(true);
    return l;
}

QLabel *wrapped(const QString &text) {
    auto *l = new QLabel(text);
    l->setWordWrap(true);
    return l;
}

QLabel *warnLabel() {
    auto *l = new QLabel;
    l->setStyleSheet(Theme::css("color: @warn;"));
    l->setWordWrap(true);
    l->hide();
    return l;
}

void setWarn(QLabel *l, const QString &text) {
    l->setText(text);
    l->setVisible(!text.isEmpty());
}

QLabel *bigCode(const QString &text, int px) {
    auto *l = new QLabel(text);
    l->setStyleSheet(Theme::css(QString("font-family: 'DejaVu Sans Mono'; font-size: %1px; font-weight: bold;").arg(px)));
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    return l;
}

QPlainTextEdit *codeArea() {
    auto *t = new QPlainTextEdit;
    t->setStyleSheet(Theme::css("font-family: 'DejaVu Sans Mono'; font-size: 11px;"));
    t->setLineWrapMode(QPlainTextEdit::NoWrap);
    t->setMinimumHeight(170);
    return t;
}

/** Sharing needs the vault (the keys are there): opened first if needed; false if the user gives up. */
bool ready(Model *model, Actions *actions) {
    if (model->teamState() == Model::TeamState::Ready || model->vaultOpen()) return true;
    bool opened = false;
    actions->unlockVault([&] { opened = true; });
    return opened;
}

/** A copy button that does not close the window: the text stays until it has been pasted somewhere. */
void copyButton(FramedDialog &d, const QString &text) {
    QPushButton *close = d.addButton(T("common.close"));
    QObject::connect(close, &QPushButton::clicked, &d, &QDialog::reject);
    QPushButton *copy = d.addButton(T("envelope.copy"));
    copy->setProperty("kind", "primary");
    QObject::connect(copy, &QPushButton::clicked, &d, [copy, text] {
        QApplication::clipboard()->setText(text);
        copy->setText(T("envelope.copied"));
    });
}

/**
 * The name a contact will have: suggested from what the other PC announces, checked by the backend as it is
 * typed (a look-alike of another contact is refused). `ok` follows whether it can be used.
 */
QWidget *contactName(Model *model, const QJsonObject &peer, QLineEdit *&field, std::shared_ptr<bool> ok, std::function<void()> changed) {
    auto *box = new QWidget;
    auto *col = new QVBoxLayout(box);
    col->setContentsMargins(0, 0, 0, 0);
    auto *row = new QHBoxLayout;
    row->addWidget(new QLabel(T("link.contactName")));
    field = new QLineEdit(peer["suggested"].toString(peer["who"].toString()));
    field->setMaxLength(64);
    row->addWidget(field, 1);
    col->addLayout(row);
    QLabel *problem = warnLabel();
    col->addWidget(problem);
    QPointer<QLabel> guard = problem;
    QString key = peer["key"].toString();
    auto check = [model, key, guard, ok, changed](const QString &text) {
        model->request("lan.nameCheck", {{"name", text}, {"key", key}}, [guard, ok, changed](const QJsonValue &v) {
            if (!guard) return;
            QString why = v.toObject()["problem"].toString();
            setWarn(guard, why);
            *ok = why.isEmpty();
            changed();
        });
    };
    QObject::connect(field, &QLineEdit::textChanged, box, check);
    check(field->text());
    return box;
}
}  // namespace

// ── Sending ──

void TeamDialogs::send(Model *model, Actions *actions, QWidget *parent, const QString &hostId) {
    const Host *h = model->host(hostId);
    if (!h || !ready(model, actions)) return;
    if (model->contacts().isEmpty()) {
        Ui::warning(parent, T("sharing.title"), T("send.noContacts"));
        model->select(Position::Team);
        return;
    }
    QString name = h->name, destination = h->destination(), auth = Host::authName(h->auth);
    // Whether a password is saved for it: the password itself stays in the backend.
    model->request("vault.has", {{"host", hostId}}, [=](const QJsonValue &has) {
        bool saved = has.toBool();
        FramedDialog d(parent, T("send.title"));
        d.body()->addWidget(new QLabel(T("send.host", name, destination)));
        d.body()->addWidget(Ui::faint(T("send.to")));
        QList<QPair<QCheckBox *, QString>> choices;
        for (const QJsonValue &v : model->contacts()) {
            QJsonObject c = v.toObject();
            auto *row = new QHBoxLayout;
            auto *b = new QCheckBox(c["name"].toString() + "  · " + T(c["nearby"].toBool() ? "send.localNetwork" : "send.asText"));
            row->addWidget(b);
            row->addWidget(Ui::faint(T("send.fingerprint", c["fingerprint"].toString())));
            row->addStretch(1);
            d.body()->addLayout(row);
            choices.append({b, c["key"].toString()});
        }
        auto *withPassword = new QCheckBox(saved ? T("send.includePassword") : T("send.noPassword"));
        withPassword->setChecked(saved);
        withPassword->setEnabled(saved);
        d.body()->addWidget(withPassword);
        d.body()->addWidget(Ui::faint(T("team.col.message")));
        auto *message = new QLineEdit;
        message->setPlaceholderText(T("send.message.prompt"));
        message->setMaxLength(500);
        d.body()->addWidget(message);
        if (auth == "KEY" || auth == "AGENT") d.body()->addWidget(note(T("send.keyNote")));
        d.body()->addWidget(note(T("send.cryptoNote")));
        d.setMinimumWidth(560);
        QPushButton *ok = d.buttons(T("hosts.send"));
        auto check = [&] { ok->setEnabled(std::any_of(choices.begin(), choices.end(), [](auto &c) { return c.first->isChecked(); })); };
        for (auto &c : choices) QObject::connect(c.first, &QCheckBox::toggled, &d, check);
        check();
        if (d.exec() != QDialog::Accepted) return;
        QJsonArray to;
        for (auto &c : choices)
            if (c.first->isChecked()) to.append(c.second);
        QString text = message->text().trimmed();
        model->request("team.send", {{"host", hostId}, {"to", to}, {"withPassword", withPassword->isChecked()}, {"message", text.isEmpty() ? QJsonValue() : QJsonValue(text)}},
                       [=](const QJsonValue &out) {
            // The contacts whose PC is not on the local network get the envelope as text, to pass on by hand.
            for (const QJsonValue &o : out.toArray()) {
                QJsonObject x = o.toObject();
                if (!x["text"].isString()) continue;
                FramedDialog e(parent, T("envelope.title"));
                QString who = x["to"].toObject()["name"].toString();
                e.body()->addWidget(new QLabel(T("envelope.for", name, who)));
                QPlainTextEdit *area = codeArea();
                area->setPlainText(x["text"].toString());
                area->setReadOnly(true);
                e.body()->addWidget(area);
                e.body()->addWidget(note(T("envelope.note", who)));
                e.setMinimumWidth(600);
                copyButton(e, x["text"].toString());
                e.exec();
            }
        }, [parent](const QString &why) { Ui::warning(parent, T("sharing.title"), T("sharing.failed", T("send.title"), why)); });
    });
}

void TeamDialogs::paste(Model *model, Actions *actions, QWidget *parent) {
    if (!ready(model, actions)) return;
    FramedDialog d(parent, T("paste.title"));
    d.body()->addWidget(Ui::faint(T("paste.label")));
    QPlainTextEdit *text = codeArea();
    text->setPlaceholderText(BEGIN + "\n…\n" + END);
    QString clip = QApplication::clipboard()->text();
    if (clip.contains(BEGIN)) text->setPlainText(clip);
    d.body()->addWidget(text);
    QLabel *error = warnLabel();
    d.body()->addWidget(error);
    d.setMinimumWidth(600);
    QPushButton *cancel = d.addButton(T("common.cancel"));
    QObject::connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    QPushButton *ok = d.addButton(T("paste.open"));
    ok->setProperty("kind", "primary");
    auto refresh = [&] { ok->setEnabled(!text->toPlainText().isEmpty()); };
    QObject::connect(text, &QPlainTextEdit::textChanged, &d, refresh);
    refresh();
    QJsonObject opened;
    auto alive = std::make_shared<bool>(true);
    // A wrong envelope keeps the window open, with the reason.
    QObject::connect(ok, &QPushButton::clicked, &d, [&, alive] {
        model->request("team.open", {{"text", text->toPlainText()}}, [&, alive](const QJsonValue &v) {
            if (!*alive) return;
            opened = v.toObject();
            d.accept();
        }, [&, alive](const QString &why) {
            if (*alive) setWarn(error, why);
        });
    });
    bool accepted = d.exec() == QDialog::Accepted;
    *alive = false;
    if (accepted && !opened.isEmpty()) accept(model, actions, parent, opened, true);
}

// ── Contacts ──

void TeamDialogs::myKey(Model *model, Actions *actions, QWidget *parent) {
    if (!ready(model, actions)) return;
    model->request("team.state", {}, [=](const QJsonValue &v) {
        QString card = v.toObject()["keyCard"].toString();
        if (card.isEmpty()) return;
        FramedDialog d(parent, T("mykey.title"));
        d.body()->addWidget(Ui::faint(T("mykey.label")));
        auto *line = new QLineEdit(card);
        line->setReadOnly(true);
        line->setStyleSheet(Theme::css("font-family: 'DejaVu Sans Mono'; font-size: 11px;"));
        d.body()->addWidget(line);
        QLabel *f = bigCode(T("team.yourFingerprint", v.toObject()["fingerprint"].toString()), 16);
        f->setWordWrap(true);
        d.body()->addWidget(f);
        d.body()->addWidget(note(T("mykey.note")));
        d.setMinimumWidth(600);
        copyButton(d, card);
        d.exec();
    });
}

/** A contact from the key card they sent, after comparing the fingerprint with them by voice or in person. */
void TeamDialogs::addContact(Model *model, Actions *actions, QWidget *parent) {
    if (!ready(model, actions)) return;
    FramedDialog d(parent, T("contact.title"));
    d.body()->addWidget(Ui::faint(T("contact.card")));
    auto *card = new QLineEdit;
    card->setPlaceholderText("LEMURI-KEY 1 …");
    card->setStyleSheet(Theme::css("font-family: 'DejaVu Sans Mono'; font-size: 11px;"));
    d.body()->addWidget(card);
    d.body()->addWidget(Ui::faint(T("contact.name")));
    auto *name = new QLineEdit;
    name->setPlaceholderText(T("contact.name.prompt"));
    name->setMaxLength(64);
    d.body()->addWidget(name);
    QLabel *fingerprint = bigCode("", 16);
    d.body()->addWidget(fingerprint);
    d.body()->addWidget(note(T("contact.compareNote")));
    auto *compared = new QCheckBox(T("contact.compared"));
    d.body()->addWidget(compared);
    QLabel *error = warnLabel();
    d.body()->addWidget(error);
    d.setMinimumWidth(600);
    QPushButton *cancel = d.addButton(T("common.cancel"));
    QObject::connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    QPushButton *ok = d.addButton(T("common.add"));
    ok->setProperty("kind", "primary");
    auto alive = std::make_shared<bool>(true);
    auto refresh = [&] { ok->setEnabled(compared->isChecked() && !fingerprint->text().isEmpty()); };
    auto read = [&, alive](const QString &text) {
        compared->setChecked(false);
        model->request("team.parseKey", {{"card", text}}, [&, alive](const QJsonValue &v) {
            if (!*alive) return;
            QJsonObject k = v.toObject();
            fingerprint->setText(k.isEmpty() ? QString() : T("contact.fingerprint", k["fingerprint"].toString()));
            if (!k.isEmpty() && name->text().trimmed().isEmpty()) name->setText(k["name"].toString());
            refresh();
        });
    };
    QObject::connect(card, &QLineEdit::textChanged, &d, read);
    QObject::connect(compared, &QCheckBox::toggled, &d, refresh);
    QString clip = QApplication::clipboard()->text().trimmed();
    if (clip.contains("LEMURI-KEY")) card->setText(clip);
    refresh();
    QObject::connect(ok, &QPushButton::clicked, &d, [&, alive] {
        model->request("team.addContact", {{"keyCard", card->text()}, {"name", name->text()}}, [&, alive](const QJsonValue &) {
            if (*alive) d.accept();
        }, [&, alive](const QString &why) {
            if (*alive) setWarn(error, why);
        });
    });
    d.exec();
    *alive = false;
}

// ── Receiving ──

void TeamDialogs::accept(Model *model, Actions *actions, QWidget *parent, const QJsonObject &i, bool justArrived) {
    QJsonObject p = i["content"].toObject();
    if (p.isEmpty()) return;
    QString from = i["from"].toString();
    const Host *twin = model->host(p["twin"].toString());
    bool password = p["hasPassword"].toBool();
    FramedDialog d(parent, justArrived ? T("accept.title.new", from) : T("accept.title", from));
    d.body()->addWidget(wrapped(T("accept.from", from, i["fingerprint"].toString())));
    d.body()->addWidget(new QLabel(T("accept.host", p["name"].toString(),
                                     p["user"].toString() + "@" + p["address"].toString() + ":" + QString::number(p["port"].toInt()))));
    QString auth = p["authMethod"].toString();
    QString access = auth == "PASSWORD" ? T("summary.auth.password") : auth == "AGENT" ? T("accept.access.agent") : T("accept.access.key");
    d.body()->addWidget(wrapped(T("accept.access", access) + (password ? "  · " + T("accept.passwordIncluded") : "")));
    if (p["message"].isString()) d.body()->addWidget(wrapped(T("accept.message", p["message"].toString())));
    d.body()->addSpacing(6);
    d.body()->addWidget(Ui::faint(T("accept.where")));
    auto *group = new QButtonGroup(&d);
    QRadioButton *update = nullptr;
    if (twin) {
        update = new QRadioButton(password ? T("accept.update", twin->name) : T("accept.updateNone", twin->name));
        update->setEnabled(password);
        group->addButton(update);
        d.body()->addWidget(update);
    }
    auto *apart = new QRadioButton(T("accept.keepApart"));
    auto *inFolder = new QRadioButton(T("accept.inFolder"));
    group->addButton(apart);
    group->addButton(inFolder);
    d.body()->addWidget(apart);
    auto *row = new QHBoxLayout;
    row->addWidget(inFolder);
    auto *folder = new QComboBox;
    folder->addItem(T("tree.datacenter"), "");
    for (const QString &f : model->folders()) folder->addItem(f, f);
    row->addWidget(folder, 1);
    d.body()->addLayout(row);
    // Never overwriting your own password as a preselected choice: it must be chosen on purpose.
    apart->setChecked(true);
    auto refresh = [&] { folder->setEnabled(inFolder->isChecked()); };
    QObject::connect(group, &QButtonGroup::buttonToggled, &d, refresh);
    refresh();
    d.setMinimumWidth(560);
    QPushButton *no = d.addButton(T("team.reject"), true);
    no->setProperty("kind", "danger");
    bool rejected = false;
    QObject::connect(no, &QPushButton::clicked, &d, [&] {
        rejected = true;
        d.reject();
    });
    QPushButton *later = d.addButton(justArrived ? T("accept.later") : T("common.cancel"));
    QObject::connect(later, &QPushButton::clicked, &d, &QDialog::reject);
    QPushButton *ok = d.addButton(T("accept.button"));
    ok->setProperty("kind", "primary");
    ok->setDefault(true);
    QObject::connect(ok, &QPushButton::clicked, &d, &QDialog::accept);
    int result = d.exec();
    if (rejected) {
        reject(model, parent, i);
        return;
    }
    if (result != QDialog::Accepted) return;
    QString mode = update && update->isChecked() ? "update" : inFolder->isChecked() ? "folder" : "apart";
    QString chosen = folder->currentData().toString();
    auto go = [=] {
        model->request("team.accept", {{"delivery", i["id"]}, {"mode", mode}, {"folder", chosen}}, [=](const QJsonValue &v) {
            if (!v.toObject()["remembered"].toBool()) Ui::warning(parent, T("team.notRemembered.title"), T("team.notRemembered", from));
        }, [parent](const QString &why) { Ui::warning(parent, T("accept.button"), T("accept.noPassword", why)); });
    };
    // The vault may have been locked meanwhile: the password needs it.
    if (password && !model->vaultOpen()) actions->unlockVault(go);
    else go();
}

void TeamDialogs::reject(Model *model, QWidget *parent, const QJsonObject &i) {
    QString from = i["from"].toString();
    model->request("team.reject", {{"delivery", i["id"]}}, [=](const QJsonValue &v) {
        if (!v.toBool()) Ui::warning(parent, T("team.notRemembered.title"), T("team.notRemembered", from));
    });
}

// ── Local network ──

/** Pairing asked from here: the code is shown and the other PC must accept; then the user confirms here too. */
void TeamDialogs::pair(Model *model, QWidget *parent, const QJsonObject &peer) {
    QString who = peer["who"].toString(), key = peer["key"].toString();
    FramedDialog d(parent, T("link.title"));
    d.body()->addWidget(wrapped(T("link.with", who, peer["address"].toString(), peer["fingerprint"].toString())));
    d.body()->addWidget(note(T("link.checkCode")));
    QLabel *number = bigCode("··· ···", 30);
    d.body()->addWidget(number);
    QLabel *wait = wrapped(T("link.exchanging", who));
    d.body()->addWidget(wait);
    auto nameOk = std::make_shared<bool>(false);
    auto accepted = std::make_shared<bool>(false);
    QPushButton *ok = nullptr;
    QLineEdit *name;
    auto refresh = [&ok, nameOk, accepted] {
        if (ok) ok->setEnabled(*nameOk && *accepted);
    };
    d.body()->addWidget(contactName(model, peer, name, nameOk, refresh));
    d.body()->addWidget(note(T("link.different")));
    d.setMinimumWidth(500);
    ok = d.buttons(T("link.sameCode"));
    refresh();
    QPointer<QLabel> n = number, w = wait;
    QMetaObject::Connection code = QObject::connect(model->backend(), &Backend::event, &d, [n, w, key, who](const QString &e, const QJsonObject &m) {
        if (e != "pairCode" || m["key"].toString() != key || !n) return;
        n->setText(m["code"].toString());
        w->setText(T("link.waiting", who));
    });
    QPointer<QPushButton> okGuard = ok;
    model->request("lan.pair", {{"key", key}}, [w, who, accepted, okGuard, nameOk](const QJsonValue &v) {
        if (!w) return;
        if (!v.toBool()) {
            w->setText(T("link.rejected", who));
            return;
        }
        w->setText(T("link.accepted", who));
        *accepted = true;
        if (okGuard) okGuard->setEnabled(*nameOk);
    }, [w](const QString &why) {
        if (w) w->setText(T("link.failed", why));
    });
    bool yes = d.exec() == QDialog::Accepted;
    QObject::disconnect(code);
    if (!yes) return;
    model->request("lan.trust", {{"key", key}, {"name", name->text()}}, nullptr,
                   [parent](const QString &why) { Ui::warning(parent, T("link.short"), T("link.cannotSave", why)); });
}

QString TeamDialogs::pairingRequest(Model *model, QWidget *parent, const QJsonObject &peer, const QString &code) {
    QString who = peer["who"].toString();
    FramedDialog d(parent, T("link.request.title"));
    d.body()->addWidget(wrapped(T("link.request.wants", who)));
    QString email = peer["email"].toString();
    d.body()->addWidget(note(T("link.request.from", peer["address"].toString(), peer["fingerprint"].toString())
                             + (email.isEmpty() ? "" : " " + T("link.request.unverified"))));
    d.body()->addWidget(note(T("link.request.same")));
    d.body()->addWidget(bigCode(code, 30));
    auto nameOk = std::make_shared<bool>(false);
    QPushButton *ok = nullptr;
    QLineEdit *name;
    d.body()->addWidget(contactName(model, peer, name, nameOk, [&ok, nameOk] {
        if (ok) ok->setEnabled(*nameOk);
    }));
    d.body()->addWidget(note(T("link.request.note")));
    d.setMinimumWidth(500);
    ok = d.buttons(T("link.sameCode"), false, T("team.reject"));
    ok->setEnabled(*nameOk);
    return d.exec() == QDialog::Accepted ? name->text() : QString();
}

void TeamDialogs::addAddress(Model *model, QWidget *parent) {
    auto a = Ui::name(parent, T("lan.add.title"), T("lan.add.label"), "", T("common.add"),
                      [](const QString &x) { return x.isEmpty() || x.contains(' ') ? T("lan.add.enter") : QString(); });
    if (!a) return;
    model->request("lan.addAddress", {{"address", *a}}, [model, parent](const QJsonValue &v) {
        QJsonObject p = v.toObject();
        if (!p["trusted"].toBool() && Ui::confirm(parent, T("lan.add.short"), T("lan.add.found", p["who"].toString(), p["address"].toString()), T("lan.link")))
            pair(model, parent, p);
    }, [parent](const QString &why) { Ui::warning(parent, T("lan.add.short"), why); });
}

/**
 * This PC waits for a link from another one while the window is open (5 minutes at most): requests that come at any
 * other time are refused without showing anything, so nobody on the network can put a made-up name on this screen.
 */
void TeamDialogs::waitForLink(Model *model, QWidget *parent) {
    FramedDialog d(parent, T("lan.wait.title"));
    auto *text = new QLabel(T("lan.wait.text"));
    text->setWordWrap(true);
    d.body()->addWidget(text);
    QLabel *left = bigCode("", 16);
    d.body()->addWidget(left);
    d.body()->addWidget(note(T("lan.wait.note")));
    d.setMinimumWidth(520);
    QObject::connect(d.addButton(T("common.close")), &QPushButton::clicked, &d, &QDialog::reject);
    QElapsedTimer clock;
    clock.start();
    constexpr int SECONDS = 5 * 60;
    QTimer tick;
    auto show = [&] {
        int s = SECONDS - int(clock.elapsed() / 1000);
        if (s <= 0) {
            d.reject();
            return;
        }
        left->setText(T("lan.wait.left", QString("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QChar('0'))));
    };
    QObject::connect(&tick, &QTimer::timeout, &d, show);
    show();
    tick.start(1000);
    model->request("lan.waitForLink", {{"on", true}});
    d.exec();
    model->request("lan.waitForLink", {{"on", false}});
}

// ── The page ──

namespace {
QTableWidget *table(const QStringList &headers) {
    auto *t = new QTableWidget(0, int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(30);
    t->setShowGrid(false);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setFocusPolicy(Qt::NoFocus);
    t->horizontalHeader()->setHighlightSections(false);
    t->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    return t;
}

void put(QTableWidget *t, int r, int c, const QString &text, bool mono = false) {
    auto *i = new QTableWidgetItem(text);
    i->setTextAlignment(Qt::AlignCenter);
    if (mono) i->setFont(QFont("DejaVu Sans Mono", 8));
    i->setToolTip(text);
    t->setItem(r, c, i);
}

void putWidget(QTableWidget *t, int r, int c, QWidget *w) {
    auto *box = new QWidget;
    auto *l = new QHBoxLayout(box);
    l->setContentsMargins(4, 0, 4, 0);
    l->setSpacing(6);
    l->setAlignment(Qt::AlignCenter);
    l->addWidget(w);
    t->setCellWidget(r, c, box);
}

/** The columns share the width in proportion to `widths`; the last one (the buttons) keeps its own. */
void widths(QTableWidget *t, const QList<int> &w) {
    for (int i = 0; i < w.size(); i++) {
        t->setColumnWidth(i, w[i]);
        t->horizontalHeader()->setSectionResizeMode(i, i == w.size() - 1 ? QHeaderView::Fixed : QHeaderView::Stretch);
    }
}

/** As tall as its rows, up to `most`: no row cut in half. */
void fit(QTableWidget *t, int most) {
    int rows = std::max(1, std::min(most, t->rowCount()));
    t->setFixedHeight(t->horizontalHeader()->height() + rows * 30 + 4);
}
}  // namespace

TeamView::TeamView(Model *model, Actions *actions, QWidget *parent) : QWidget(parent), model(model), actions(actions) {
    auto *outer = new QVBoxLayout(this);
    outer->setContentsMargins(0, 0, 0, 0);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::NoFrame);
    auto *page = new QWidget;
    auto *col = new QVBoxLayout(page);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(8);
    scroll->setWidget(page);
    outer->addWidget(scroll);

    // ── You ──
    auto *you = new QWidget;
    auto *yc = new QVBoxLayout(you);
    yc->setContentsMargins(0, 0, 0, 0);
    auto *head = new QHBoxLayout;
    stateChip = Ui::chip("warn", "");
    head->addWidget(stateChip);
    message = wrapped("");
    head->addWidget(message, 1);
    yc->addLayout(head);
    fingerprint = Ui::faint("");
    fingerprint->setWordWrap(true);
    fingerprint->setTextInteractionFlags(Qt::TextSelectableByMouse);
    yc->addWidget(fingerprint);
    openVault = Ui::primary(T("team.openVault"), "ok", [actions] { actions->unlockVault(); });
    yc->addWidget(openVault, 0, Qt::AlignLeft);
    yc->addWidget(note(T("team.howItWorks")));
    col->addWidget(Ui::panel(T("team.yourKey"), you));

    // ── Incoming ──
    auto *in = new QWidget;
    auto *ic = new QVBoxLayout(in);
    ic->setContentsMargins(0, 0, 0, 0);
    incoming = table({T("team.col.from"), T("team.col.fingerprint"), T("common.host"), T("host.address"), T("auth.password"),
                      T("team.col.message"), T("team.col.received"), ""});
    QList<int> iw{150, 310, 140, 190, 80, 180, 140, 230};
    widths(incoming, iw);
    ic->addWidget(incoming);
    auto *pr = new QHBoxLayout;
    pr->addWidget(Ui::primary(T("paste.button"), "inbox", [this] { TeamDialogs::paste(this->model, this->actions, this); }));
    pr->addWidget(Ui::faint(T("paste.note")));
    pr->addStretch(1);
    ic->addLayout(pr);
    auto *incomingPanel = Ui::panel(T("team.incoming", 0), in);
    incomingTitle = incomingPanel->findChildren<QLabel *>().first();
    col->addWidget(incomingPanel);

    // ── Contacts ──
    auto *cb = new QWidget;
    auto *cc = new QVBoxLayout(cb);
    cc->setContentsMargins(0, 0, 0, 0);
    contacts = table({T("common.status"), T("host.name"), T("team.col.fingerprint"), ""});
    QList<int> cw{130, 220, 310, 120};
    widths(contacts, cw);
    cc->addWidget(contacts);
    auto *cr = new QHBoxLayout;
    cr->addWidget(Ui::button(T("contact.title"), "plus", [this] { TeamDialogs::addContact(this->model, this->actions, this); }));
    cr->addWidget(Ui::button(T("mykey.title"), "right", [this] { TeamDialogs::myKey(this->model, this->actions, this); }));
    cr->addWidget(Ui::faint(T("contacts.note")));
    cr->addStretch(1);
    cc->addLayout(cr);
    col->addWidget(Ui::panel(T("contacts.title"), cb));

    // ── Local network ──
    auto *lb = new QWidget;
    auto *lc = new QVBoxLayout(lb);
    lc->setContentsMargins(0, 0, 0, 0);
    auto *lr = new QHBoxLayout;
    lanOn = new QCheckBox(T("lan.enable"));
    connect(lanOn, &QCheckBox::clicked, this, [this](bool on) { this->model->request("lan.on", {{"on", on}}); });
    lr->addWidget(lanOn);
    lanMessage = wrapped("");
    lr->addWidget(lanMessage, 1);
    lc->addLayout(lr);
    firewall = warnLabel();
    lc->addWidget(firewall);
    nearby = table({T("common.status"), T("host.name"), T("host.address"), T("team.col.fingerprint"), ""});
    QList<int> nw{130, 200, 130, 310, 120};
    widths(nearby, nw);
    lc->addWidget(nearby);
    peerWarnings = warnLabel();
    lc->addWidget(peerWarnings);
    auto *nr = new QHBoxLayout;
    byAddress = Ui::button(T("lan.addByAddress"), "plus", [this] { TeamDialogs::addAddress(this->model, this); });
    nr->addWidget(byAddress);
    waitLink = Ui::button(T("lan.wait.button"), "download", [this] { TeamDialogs::waitForLink(this->model, this); });
    nr->addWidget(waitLink);
    nr->addWidget(Ui::faint(T("lan.trustNote")));
    nr->addStretch(1);
    lc->addLayout(nr);
    col->addWidget(Ui::panel(T("lan.title"), lb, Ui::action("refresh", T("lan.refresh"), [this] { this->model->request("lan.refresh"); })));
    col->addStretch(1);

    connect(model, &Model::teamChanged, this, &TeamView::fill);
    connect(model, &Model::vaultChanged, this, &TeamView::fill);
    model->refreshTeam();
    fill();
}

void TeamView::fill() {
    bool ok = model->teamState() == Model::TeamState::Ready;
    QString tone = ok ? "ok" : "warn";
    stateChip->setText(T(ok ? "sharing.state.ready" : "sharing.state.vaultLocked"));
    stateChip->setStyleSheet(Ui::chipStyle(tone));
    message->setText(model->teamMessage());
    fingerprint->setText(model->fingerprint().isEmpty() ? QString() : T("team.yourFingerprint", model->fingerprint()));
    openVault->setVisible(!ok);

    // Incoming
    incomingTitle->setText(T("team.incoming", int(model->deliveryList().size())));
    incoming->setRowCount(int(model->deliveryList().size()));
    for (int r = 0; r < model->deliveryList().size(); r++) {
        QJsonObject i = model->deliveryList()[r].toObject();
        QJsonObject p = i["content"].toObject();
        put(incoming, r, 0, i["from"].toString());
        put(incoming, r, 1, i["fingerprint"].toString(), true);
        put(incoming, r, 2, p.isEmpty() ? "?" : p["name"].toString());
        put(incoming, r, 3, p.isEmpty() ? i["error"].toString() : p["user"].toString() + "@" + p["address"].toString() + ":" + QString::number(p["port"].toInt()), true);
        if (!p.isEmpty())
            putWidget(incoming, r, 4, p["hasPassword"].toBool() ? Ui::chip("info", T("team.password.included")) : Ui::chip("neutral", T("team.password.no")));
        put(incoming, r, 5, p["message"].toString());
        put(incoming, r, 6, Ui::dateTime(qint64(i["when"].toDouble())));
        auto *buttons = new QWidget;
        auto *b = new QHBoxLayout(buttons);
        b->setContentsMargins(0, 0, 0, 0);
        b->setSpacing(6);
        QPushButton *accept = Ui::primary(T("team.accept"), "ok", [this, i] { TeamDialogs::accept(model, actions, this, i, false); });
        accept->setEnabled(!p.isEmpty());
        b->addWidget(accept);
        b->addWidget(Ui::button(T("team.reject"), "close", [this, i] {
            if (Ui::confirm(this, T("team.reject"), T("team.reject.ask", i["from"].toString()), T("team.reject"), true)) TeamDialogs::reject(model, this, i);
        }));
        putWidget(incoming, r, 7, buttons);
    }
    if (model->deliveryList().isEmpty()) {
        incoming->setRowCount(1);
        put(incoming, 0, 0, "");
        auto *none = new QTableWidgetItem(T("team.incoming.none"));
        none->setForeground(Theme::faint);
        incoming->setItem(0, 2, none);
    }
    fit(incoming, 5);

    // Contacts
    contacts->setRowCount(int(model->contacts().size()));
    for (int r = 0; r < model->contacts().size(); r++) {
        QJsonObject c = model->contacts()[r].toObject();
        putWidget(contacts, r, 0, c["nearby"].toBool() ? Ui::chip("ok", T("contacts.nearby")) : Ui::chip("neutral", T("contacts.byText")));
        put(contacts, r, 1, c["name"].toString());
        put(contacts, r, 2, c["fingerprint"].toString(), true);
        QString key = c["key"].toString(), name = c["name"].toString();
        putWidget(contacts, r, 3, Ui::button(T("common.remove"), "trash", [this, key, name] {
            if (!Ui::confirm(this, T("contacts.remove"), T("contacts.remove.ask", name), T("common.remove"), true)) return;
            model->request("lan.forget", {{"key", key}}, nullptr, [this](const QString &why) { Ui::warning(this, T("contacts.remove"), why); });
        }));
    }
    if (model->contacts().isEmpty()) {
        contacts->setRowCount(1);
        auto *none = new QTableWidgetItem(T("contacts.none"));
        none->setForeground(Theme::faint);
        none->setTextAlignment(Qt::AlignCenter);
        contacts->setItem(0, 1, none);
    }
    fit(contacts, 8);

    // Local network
    lanOn->setChecked(model->lanOn());
    lanMessage->setText(model->lanMessage());
    setWarn(firewall, model->firewall());
    byAddress->setEnabled(model->lanState() == Model::LanState::Active);
    waitLink->setEnabled(model->lanState() == Model::LanState::Active);
    nearby->setRowCount(int(model->peers().size()));
    QStringList warnings;
    for (int r = 0; r < model->peers().size(); r++) {
        QJsonObject p = model->peers()[r].toObject();
        bool trusted = p["trusted"].toBool(), near = p["nearby"].toBool();
        bool problem = !p["warning"].isNull() && !p["warning"].toString().isEmpty() && near;
        QLabel *chip = problem ? Ui::chip("error", T("lan.firewall"))
                     : trusted ? Ui::chip(near ? "ok" : "neutral", near ? T("lan.linked") : T("lan.linkedAway"))
                     : Ui::chip("warn", T("lan.notLinked"));
        if (problem) {
            chip->setToolTip(p["who"].toString() + ": " + p["warning"].toString());
            warnings << p["who"].toString() + " (" + p["address"].toString() + "): " + p["warning"].toString();
        }
        putWidget(nearby, r, 0, chip);
        put(nearby, r, 1, p["trustedName"].isString() ? p["trustedName"].toString() : p["who"].toString());
        put(nearby, r, 2, p["address"].toString());
        put(nearby, r, 3, p["fingerprint"].toString(), true);
        QString key = p["key"].toString(), who = p["who"].toString();
        if (trusted)
            putWidget(nearby, r, 4, Ui::button(T("lan.unlink"), "plug-off", [this, key, who] {
                if (!Ui::confirm(this, T("lan.unlink"), T("lan.unlink.ask", who), T("lan.unlink"), true)) return;
                model->request("lan.forget", {{"key", key}}, nullptr, [this](const QString &why) { Ui::warning(this, T("lan.unlink"), why); });
            }));
        else
            putWidget(nearby, r, 4, Ui::primary(T("lan.link"), "plug", [this, p] { TeamDialogs::pair(model, this, p); }));
    }
    if (model->peers().isEmpty()) {
        nearby->setRowCount(1);
        auto *none = new QTableWidgetItem(T("lan.none"));
        none->setForeground(Theme::faint);
        nearby->setItem(0, 1, none);
        nearby->setSpan(0, 1, 1, 3);
    } else {
        nearby->clearSpans();
    }
    fit(nearby, 6);
    setWarn(peerWarnings, warnings.join("\n"));
}
