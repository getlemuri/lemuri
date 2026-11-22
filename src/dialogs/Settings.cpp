#include "dialogs/Settings.h"

#include "app/Backend.h"
#include "app/Model.h"
#include "app/Texts.h"
#include "terminal/TerminalView.h"
#include "widgets/Icons.h"
#include "widgets/Theme.h"
#include "widgets/Ui.h"
#include "window/Actions.h"
#include "window/Splash.h"

#include <QApplication>
#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QDate>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QRadioButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QTableWidget>
#include <QUrl>
#include <QVBoxLayout>
#include <memory>

namespace {
QLabel *note(const QString &text) {
    QLabel *l = Ui::faint(text);
    l->setWordWrap(true);
    return l;
}

QLabel *warnLabel() {
    auto *l = new QLabel;
    l->setStyleSheet(Theme::css("color: @warn;"));
    l->setWordWrap(true);
    return l;
}

/** The files of ~/.ssh as a table: a check box, the name in monospace, the kind and a detail. */
QTableWidget *sshTable(const QJsonArray &rows, const QString &detailTitle, QList<QCheckBox *> &boxes) {
    auto *t = new QTableWidget(int(rows.size()), 4);
    t->setHorizontalHeaderLabels({"", T("backup.col.file"), T("backup.col.kind"), detailTitle});
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(28);
    t->setSelectionMode(QAbstractItemView::NoSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setFocusPolicy(Qt::NoFocus);
    t->setShowGrid(false);
    t->horizontalHeader()->setStretchLastSection(true);
    t->setColumnWidth(0, 36);
    t->setColumnWidth(1, 160);
    t->setColumnWidth(2, 160);
    for (int i = 0; i < rows.size(); i++) {
        QJsonObject r = rows[i].toObject();
        auto *c = new QCheckBox;
        c->setChecked(r["chosen"].toBool());
        boxes << c;
        auto *cell = new QWidget;
        auto *l = new QHBoxLayout(cell);
        l->setContentsMargins(0, 0, 0, 0);
        l->setAlignment(Qt::AlignCenter);
        l->addWidget(c);
        t->setCellWidget(i, 0, cell);
        auto *name = new QTableWidgetItem(r["name"].toString());
        name->setFont(QFont("DejaVu Sans Mono", 9));
        name->setTextAlignment(Qt::AlignCenter);
        t->setItem(i, 1, name);
        auto *kind = new QTableWidgetItem(r["kind"].toString());
        kind->setTextAlignment(Qt::AlignCenter);
        t->setItem(i, 2, kind);
        QString d = r["detail"].toString();
        auto *detail = new QTableWidgetItem(d.isEmpty() ? "-" : d);
        detail->setTextAlignment(Qt::AlignCenter);
        detail->setToolTip(r["tip"].toString(d));
        if (d.isEmpty()) detail->setForeground(Theme::faint);
        t->setItem(i, 3, detail);
    }
    int shown = std::max(1, std::min(6, int(rows.size())));
    t->setFixedHeight(t->horizontalHeader()->sizeHint().height() + shown * 28 + 4);
    return t;
}

QString readable(QString markdown) {
    if (markdown.trimmed().isEmpty()) return T("update.noNotes");
    QString b;
    for (QString l : markdown.remove('\r').split('\n')) {
        while (l.endsWith(' ')) l.chop(1);
        if (l.startsWith('#')) {
            if (!b.isEmpty() && !b.endsWith("\n\n")) b += '\n';
            b += l.remove(QRegularExpression("^#+\\s*")).toUpper() + '\n';
        } else if (QRegularExpression("^\\s*[-*] ").match(l).hasMatch()) {
            b += l.replace(QRegularExpression("[-*] "), "• ") + '\n';
        } else {
            b += l + '\n';
        }
    }
    return b.remove("**").remove('`').trimmed();
}
}  // namespace

// ── Backup ──

void BackupDialogs::exportAll(Model *model, Actions *actions, QWidget *parent) {
    // The passwords are part of it: the vault must be open.
    actions->unlockVault([=] {
        model->request("backup.sshFiles", {}, [=](const QJsonValue &v) {
            QJsonObject r = v.toObject();
            QJsonArray files = r["files"].toArray();
            int min = r["minPassphrase"].toInt(10);
            FramedDialog d(parent, T("backup.export.title"));
            auto *what = new QLabel(T("backup.export.what", r["hosts"].toInt(), r["folders"].toInt(), r["secrets"].toInt()));
            what->setWordWrap(true);
            d.body()->addWidget(what);
            d.body()->addWidget(new QLabel(T("backup.sshKeys")));
            QList<QCheckBox *> boxes;
            if (files.isEmpty()) d.body()->addWidget(Ui::faint(T("backup.noKeys", r["dir"].toString())));
            else d.body()->addWidget(sshTable(files, T("backup.col.usedBy"), boxes));
            d.body()->addWidget(new QLabel(T("backup.passphrase")));
            auto *pass = new QLineEdit;
            pass->setEchoMode(QLineEdit::Password);
            d.body()->addWidget(pass);
            d.body()->addWidget(new QLabel(T("vault.repeat")));
            auto *again = new QLineEdit;
            again->setEchoMode(QLineEdit::Password);
            d.body()->addWidget(again);
            QLabel *problem = warnLabel();
            d.body()->addWidget(problem);
            d.body()->addWidget(note(T("backup.passphraseNote")));
            d.setMinimumWidth(680);
            QPushButton *ok = d.buttons(T("backup.export.button"));
            auto check = [&] {
                QString why = pass->text().size() < min ? T("backup.passphraseShort", min) : pass->text() != again->text() ? T("backup.passphraseDiffers") : QString();
                problem->setText(why);
                ok->setEnabled(why.isEmpty());
            };
            QObject::connect(pass, &QLineEdit::textChanged, &d, check);
            QObject::connect(again, &QLineEdit::textChanged, &d, check);
            check();
            pass->setFocus();
            if (d.exec() != QDialog::Accepted) return;
            QString to = QFileDialog::getSaveFileName(parent, T("backup.export.title"),
                                                      QDir::homePath() + "/lemuri-backup-" + QDate::currentDate().toString(Qt::ISODate) + ".lemuri",
                                                      T("backup.fileType") + " (*.lemuri)");
            if (to.isEmpty()) return;
            QJsonArray chosen;
            for (int i = 0; i < boxes.size(); i++)
                if (boxes[i]->isChecked()) chosen.append(files[i].toObject()["name"]);
            model->request("backup.export", {{"path", to}, {"passphrase", pass->text()}, {"files", chosen}}, [parent](const QJsonValue &out) {
                Ui::warning(parent, T("backup.export.title"), out.toObject()["message"].toString());
            }, [parent](const QString &why) { Ui::warning(parent, T("backup.export.title"), T("backup.export.failed", why)); });
        });
    });
}

void BackupDialogs::importFrom(Model *model, Actions *actions, QWidget *parent) {
    QString from = QFileDialog::getOpenFileName(parent, T("backup.import.title"), QDir::homePath(),
                                                T("backup.fileType") + " (*.lemuri);;" + T("backup.allFiles") + " (*)");
    if (from.isEmpty()) return;
    // The passphrase, asked until it opens the file: with a wrong one the window stays open.
    FramedDialog d(parent, T("backup.import.title"));
    d.body()->addWidget(new QLabel(T("backup.passphrase")));
    auto *pass = new QLineEdit;
    pass->setEchoMode(QLineEdit::Password);
    pass->setMinimumWidth(420);
    d.body()->addWidget(pass);
    QLabel *error = warnLabel();
    d.body()->addWidget(error);
    QPushButton *cancel = d.addButton(T("common.cancel"));
    QObject::connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
    QPushButton *ok = d.addButton(T("backup.open"));
    ok->setProperty("kind", "primary");
    ok->setDefault(true);
    QJsonObject opened;
    auto alive = std::make_shared<bool>(true);
    QObject::connect(ok, &QPushButton::clicked, &d, [&, alive] {
        ok->setEnabled(false);
        model->request("backup.open", {{"path", from}, {"passphrase", pass->text()}}, [&, alive](const QJsonValue &v) {
            if (!*alive) return;
            opened = v.toObject();
            d.accept();
        }, [&, alive](const QString &why) {
            if (!*alive) return;
            ok->setEnabled(true);
            error->setText(why);
            pass->selectAll();
        });
    });
    pass->setFocus();
    bool yes = d.exec() == QDialog::Accepted;
    *alive = false;
    if (!yes || opened.isEmpty()) return;
    QString token = opened["token"].toString();
    // The passwords go into this PC's vault: open it, or create it the first time.
    bool unlocked = false;
    actions->unlockVault([&] { unlocked = true; });
    if (!unlocked) {
        model->request("backup.close", {{"token", token}});
        return;
    }

    FramedDialog i(parent, T("backup.import.title"));
    auto *what = new QLabel(T("backup.import.what", opened["from"].toString(), Ui::dateTime(qint64(opened["created"].toDouble())),
                              opened["hosts"].toInt(), opened["folders"].toInt(), opened["secrets"].toInt()));
    what->setWordWrap(true);
    i.body()->addWidget(what);
    auto *merge = new QRadioButton(T("backup.merge"));
    auto *replace = new QRadioButton(T("backup.replace"));
    auto *group = new QButtonGroup(&i);
    group->addButton(merge);
    group->addButton(replace);
    // A PC with nothing yet takes everything; otherwise adding is the safe choice.
    (opened["replaceByDefault"].toBool() ? replace : merge)->setChecked(true);
    i.body()->addWidget(merge);
    i.body()->addWidget(note(T("backup.merge.note")));
    i.body()->addWidget(replace);
    i.body()->addWidget(note(T("backup.replace.note")));
    i.body()->addWidget(new QLabel(T("backup.import.ssh", "~/.ssh")));
    QJsonArray files = opened["files"].toArray();
    QList<QCheckBox *> boxes;
    if (files.isEmpty()) i.body()->addWidget(Ui::faint(T("backup.import.noSsh")));
    else i.body()->addWidget(sshTable(files, T("backup.col.state"), boxes));
    i.body()->addWidget(note(T("backup.import.sshNote")));
    i.setMinimumWidth(680);
    i.buttons(T("backup.import.button"));
    int here = opened["hostsHere"].toInt();
    if (i.exec() != QDialog::Accepted
        || (replace->isChecked() && here > 0 && !Ui::confirm(parent, T("backup.import.title"), T("backup.replace.ask", here), T("backup.replace.button"), true))) {
        model->request("backup.close", {{"token", token}});
        return;
    }
    QJsonArray chosen;
    for (int k = 0; k < boxes.size(); k++)
        if (boxes[k]->isChecked()) chosen.append(files[k].toObject()["name"]);
    model->request("backup.import", {{"token", token}, {"replace", replace->isChecked()}, {"files", chosen}, {"source", QFileInfo(from).fileName()}},
                   [parent](const QJsonValue &v) { Ui::warning(parent, T("backup.import.title"), v.toObject()["message"].toString()); },
                   [parent](const QString &why) { Ui::warning(parent, T("backup.import.title"), why); });
}

// ── Updates ──

void UpdateDialogs::offer(Model *model, QWidget *parent, const QJsonObject &r) {
    QString tag = r["tag"].toString();
    model->request("update.blocker", {}, [=](const QJsonValue &blocker) {
        FramedDialog d(parent, T("update.title", tag));
        d.body()->addWidget(new QLabel(T("update.current", model->version())));
        d.body()->addWidget(new QLabel(T("update.whatsNew")));
        auto *notes = new QPlainTextEdit(readable(r["notes"].toString()));
        notes->setReadOnly(true);
        notes->setMinimumSize(560, 280);
        d.body()->addWidget(notes);
        if (blocker.isString()) d.body()->addWidget(note(T("update.manual", blocker.toString())));
        QPushButton *page = d.addButton(T("update.page"), true);
        QObject::connect(page, &QPushButton::clicked, &d, [r] { QDesktopServices::openUrl(QUrl(r["url"].toString())); });
        QPushButton *skip = d.addButton(T("update.skip"));
        int choice = 0;
        QObject::connect(skip, &QPushButton::clicked, &d, [&] {
            choice = 1;
            d.reject();
        });
        QPushButton *later = d.addButton(T("update.later"));
        QObject::connect(later, &QPushButton::clicked, &d, &QDialog::reject);
        QPushButton *update = d.addButton(T("update.install"));
        update->setProperty("kind", "primary");
        update->setEnabled(!blocker.isString());
        QObject::connect(update, &QPushButton::clicked, &d, &QDialog::accept);
        if (d.exec() != QDialog::Accepted) {
            if (choice == 1) model->request("update.skip", {{"version", r["version"]}});
            else model->request("update.later");
            return;
        }
        // The steps one under the other while git and Maven work; nothing to press until they end.
        auto *w = new FramedDialog(parent, T("update.installing", tag));
        w->setAttribute(Qt::WA_DeleteOnClose);
        QLabel *now = new QLabel(T("update.wait"));
        w->body()->addWidget(now);
        auto *steps = new QVBoxLayout;
        w->body()->addLayout(steps);
        QLabel *wait = note(T("update.wait.note"));
        w->body()->addWidget(wait);
        w->setMinimumWidth(560);
        QPushButton *close = w->addButton(T("common.close"));
        close->setEnabled(false);
        QObject::connect(close, &QPushButton::clicked, w, &QDialog::reject);
        QPointer<FramedDialog> guard = w;
        QMetaObject::Connection c = QObject::connect(model->backend(), &Backend::event, w, [guard, steps, now](const QString &e, const QJsonObject &m) {
            if (e != "updateStep" || !guard) return;
            steps->addWidget(new QLabel("• " + m["step"].toString()));
            now->setText(m["step"].toString());
        });
        model->request("update.install", {}, [=](const QJsonValue &) {
            if (!guard) return;
            QObject::disconnect(c);
            now->setText(T("update.done", tag));
            wait->hide();
            // The backend starts the new copy once this one has ended (the installed package: /usr/bin/lemuri).
            QPushButton *quit = guard->addButton(T("update.restart"));
            quit->setProperty("kind", "primary");
            QObject::connect(quit, &QPushButton::clicked, qApp, [model] {
                model->request("update.restart", {}, [](const QJsonValue &) { QApplication::quit(); },
                               [](const QString &) { QApplication::quit(); });
            });
            close->setEnabled(true);
        }, [=](const QString &why) {
            if (!guard) return;
            QObject::disconnect(c);
            now->setText(T("update.failed"));
            wait->setText(why);
            close->setEnabled(true);
        });
        w->open();
    });
}

// ── Settings ──

namespace {
/** A group with its title and the items indented below. */
QWidget *group(const QString &title, const QList<QWidget *> &items) {
    auto *g = new QWidget;
    auto *col = new QVBoxLayout(g);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(8);
    auto *t = new QLabel(title);
    t->setStyleSheet(Theme::css("font-weight: bold; font-size: 13px;"));
    col->addWidget(t);
    auto *inside = new QVBoxLayout;
    inside->setContentsMargins(14, 0, 0, 0);
    inside->setSpacing(8);
    for (QWidget *w : items) inside->addWidget(w);
    col->addLayout(inside);
    return g;
}

QWidget *line(const QList<QWidget *> &buttons) {
    auto *w = new QWidget;
    auto *r = new QHBoxLayout(w);
    r->setContentsMargins(0, 0, 0, 0);
    r->setSpacing(8);
    for (QWidget *b : buttons) r->addWidget(b);
    r->addStretch(1);
    return w;
}

QWidget *page(const QList<QWidget *> &groups) {
    auto *p = new QWidget;
    auto *col = new QVBoxLayout(p);
    col->setContentsMargins(22, 4, 22, 16);
    col->setSpacing(22);
    for (QWidget *g : groups) col->addWidget(g);
    col->addStretch(1);
    return p;
}
}  // namespace

namespace {
/** The settings applied with OK or Apply (the vault, backup and update buttons act at once). */
struct Choices {
    QComboBox *language = nullptr, *probe = nullptr;
    QCheckBox *splash = nullptr, *types = nullptr, *lan = nullptr, *carrierGrade = nullptr, *automatic = nullptr;
    static inline const QStringList CODES{"auto", "en", "it"};
    static inline const QList<int> PROBE_MINUTES{1, 5, 0};

    /** All of them in one string: Apply is on only if it differs from the one saved. */
    QString state() const {
        return QString("%1|%2|%3|%4|%5|%6|%7").arg(language->currentIndex()).arg(probe->currentIndex()).arg(types->isChecked())
                .arg(lan->isChecked()).arg(automatic->isChecked()).arg(splash->isChecked()).arg(carrierGrade->isChecked());
    }
};

struct Page {
    QString name, icon;
    QWidget *body;
};

/** The language (the app has only the dark theme and its own frame) and the splash screen. */
QWidget *appearancePage(const QJsonObject &ui, Choices &c) {
    c.language = new QComboBox;
    c.language->addItems({T("menu.language.auto"), "English", "Italiano"});
    c.language->setCurrentIndex(std::max(0, int(Choices::CODES.indexOf(ui["language"].toString("auto")))));
    auto *languageRow = line({new QLabel(T("settings.language")), c.language});
    c.splash = new QCheckBox(T("menu.splash"));
    c.splash->setChecked(ui["splash"].toString("true") != "false");
    return page({group(T("settings.language"), {languageRow, note(T("settings.afterRestart"))}), group(T("settings.window"), {c.splash})});
}

/** The vault's state, and its buttons, which act at once. */
QWidget *vaultPage(Model *model, Actions *actions, QDialog &d) {
    auto *vaultState = new QLabel;
    QPushButton *lockOrOpen = Ui::button("", "lock");
    QPushButton *change = Ui::button(T("menu.changeMaster"), "edit", [actions] { actions->changeVaultPassword(); });
    auto onVault = [=] {
        vaultState->setText(model->vaultOpen() ? T("status.vault.open") : T("status.vault.locked"));
        lockOrOpen->setText(model->vaultOpen() ? T("menu.vault.lock") : model->vaultExists() ? T("vault.unlock.title") : T("vault.create.title"));
        change->setEnabled(model->vaultExists());
    };
    QObject::connect(lockOrOpen, &QPushButton::clicked, &d, [actions] { actions->toggleVault(); });
    QObject::connect(model, &Model::vaultChanged, &d, onVault);
    onVault();
    return page({group(T("common.status"), {vaultState, note(T("settings.vault.note"))}), group(T("settings.actions"), {line({lockOrOpen, change})})});
}

QWidget *backupPage(Model *model, Actions *actions, QWidget *parent) {
    return page({group(T("settings.backup"), {note(T("settings.backup.note")),
        line({Ui::button(T("backup.export.menu"), "upload", [=] { BackupDialogs::exportAll(model, actions, parent); }),
              Ui::button(T("backup.import.menu"), "download", [=] { BackupDialogs::importFrom(model, actions, parent); })})})});
}

/** How often the probe runs; whether Lemuri may type commands into the terminal. */
QWidget *serversPage(const QJsonObject &ui, Choices &c) {
    c.probe = new QComboBox;
    c.probe->addItems({T("settings.probe.every1"), T("settings.probe.every5"), T("settings.probe.never")});
    int minutes = ui["probeMinutes"].toString("5").toInt();
    c.probe->setCurrentIndex(minutes == 1 ? 0 : minutes == 0 ? 2 : 1);
    c.types = new QCheckBox(T("settings.typesCommands"));
    c.types->setChecked(ui["terminalCommands"].toString() == "true");
    return page({group(T("settings.probe"), {c.probe, note(T("settings.probe.note"))}),
                 group(T("settings.terminal"), {c.types, note(T("settings.typesCommands.note"))})});
}

QWidget *networkPage(Model *model, Choices &c) {
    c.lan = new QCheckBox(T("lan.enable"));
    c.lan->setChecked(model->lanOn());
    c.carrierGrade = new QCheckBox(T("settings.lan.carrierGrade"));
    c.carrierGrade->setChecked(model->lanCarrierGrade());
    return page({group(T("settings.lan"), {c.lan, note(T("settings.lan.note")), c.carrierGrade, note(T("settings.lan.carrierGrade.note"))})});
}

/** The last check, a check now, what is new; whether to check every day. */
QWidget *updatesPage(Model *model, QWidget *parent, const QJsonObject &up, Choices &c, QDialog &d) {
    auto *updateState = new QLabel;
    updateState->setWordWrap(true);
    QPushButton *show = Ui::primary(T("settings.updates.show"), "download", [=] {
        if (!model->updateAvailable().isEmpty()) UpdateDialogs::offer(model, parent, model->updateAvailable());
    });
    auto onUpdates = [=](qint64 last) {
        QJsonObject r = model->updateAvailable();
        updateState->setText(!r.isEmpty() ? T("settings.updates.available", r["tag"].toString())
                             : last == 0 ? T("settings.updates.never")
                             : T("settings.updates.latest", model->version(), Ui::dateTime(last)));
        show->setVisible(!r.isEmpty());
    };
    onUpdates(qint64(up["lastCheck"].toDouble()));
    QPushButton *check = Ui::button(T("settings.updates.check"), "refresh");
    QPointer<QLabel> stateGuard = updateState;
    QObject::connect(check, &QPushButton::clicked, &d, [=] {
        updateState->setText(T("settings.updates.checking"));
        check->setEnabled(false);
        model->request("update.check", {}, [=](const QJsonValue &r) {
            if (!stateGuard) return;
            check->setEnabled(true);
            model->setUpdateAvailable(r.toObject());
            onUpdates(QDateTime::currentMSecsSinceEpoch());
            // Asked by hand: what is new opens at once.
            if (r.isObject()) UpdateDialogs::offer(model, parent, r.toObject());
        }, [=](const QString &why) {
            if (!stateGuard) return;
            check->setEnabled(true);
            updateState->setText(T("settings.updates.error", why));
        });
    });
    c.automatic = new QCheckBox(T("settings.updates.automatic"));
    c.automatic->setChecked(up["automatic"].toBool(true));
    return page({group(T("common.status"), {updateState, line({check, show})}),
                 group(T("settings.updates.automaticGroup"), {c.automatic, note(T("settings.updates.note"))})});
}

QWidget *aboutPage(Model *model) {
    auto *about = new QWidget;
    auto *g = new QGridLayout(about);
    g->setContentsMargins(0, 0, 0, 0);
    g->setHorizontalSpacing(16);
    QString data = model->info()["data"].toString(QDir::homePath() + "/.lemuri");
    QList<QPair<QString, QString>> rows{{T("settings.version"), model->version()}, {T("settings.user"), model->info()["user"].toString()},
                                       {T("settings.data"), data}, {T("settings.logs"), data + "/logs"},
                                       {"Qt", qVersion()}};
    for (int i = 0; i < rows.size(); i++) {
        g->addWidget(Ui::faint(rows[i].first), i, 0);
        g->addWidget(i == 2 || i == 3 ? Ui::code(rows[i].second) : new QLabel(rows[i].second), i, 1);
    }
    g->setColumnStretch(1, 1);
    return page({group("Lemuri", {about})});
}

/** The list of sections with the search on the left, the page on the right; the search field in `search`. */
QWidget *sections(const QList<Page> &pages, QDialog &d, QLineEdit **search) {
    auto *body = new QWidget;
    auto *row = new QHBoxLayout(body);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(0);
    auto *left = new QWidget;
    left->setObjectName("settingsLeft");
    left->setAttribute(Qt::WA_StyledBackground);
    left->setStyleSheet(Theme::css("#settingsLeft { background: @border; border-right: 1px solid @edge; }"
                        "QListWidget { background: transparent; border: none; outline: none; }"
                        "QListWidget::item { padding: 5px 10px; border-top-left-radius: 4px; border-bottom-left-radius: 4px; }"
                        "QListWidget::item:hover { background: @controlHover; }"
                        "QListWidget::item:selected { background: @listSelected; color: @text; font-weight: bold; }"));
    left->setFixedWidth(220);
    auto *lc = new QVBoxLayout(left);
    lc->setContentsMargins(10, 10, 0, 10);
    auto *field = new QLineEdit;
    field->setPlaceholderText(T("settings.search"));
    lc->addWidget(field);
    auto *list = new QListWidget;
    list->setIconSize(QSize(15, 15));
    for (const Page &p : pages) list->addItem(new QListWidgetItem(Icons::icon(p.icon, 15, Theme::text), p.name));
    lc->addWidget(list, 1);
    row->addWidget(left);
    auto *right = new QWidget;
    auto *rc = new QVBoxLayout(right);
    rc->setContentsMargins(0, 12, 0, 0);
    auto *title = new QLabel;
    title->setStyleSheet(Theme::css("font-size: 17px; font-weight: 800; padding: 0 22px;"));
    rc->addWidget(title);
    auto *stack = new QStackedWidget;
    for (const Page &p : pages) {
        auto *scroll = new QScrollArea;
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setWidget(p.body);
        stack->addWidget(scroll);
    }
    auto *empty = new QLabel(T("settings.noResults"));
    empty->setStyleSheet(Theme::css("color: @faint; padding: 22px;"));
    empty->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    stack->addWidget(empty);
    rc->addWidget(stack, 1);
    row->addWidget(right, 1);
    QObject::connect(list, &QListWidget::currentRowChanged, &d, [=](int i) {
        if (i < 0) return;
        title->setText(pages[i].name);
        stack->setCurrentIndex(i);
    });
    // The search keeps the sections whose name or texts contain it.
    QObject::connect(field, &QLineEdit::textChanged, &d, [=](const QString &text) {
        QString q = text.trimmed().toLower();
        int first = -1;
        for (int i = 0; i < pages.size(); i++) {
            bool in = q.isEmpty() || pages[i].name.toLower().contains(q);
            for (QLabel *l : pages[i].body->findChildren<QLabel *>()) in = in || l->text().toLower().contains(q);
            for (QAbstractButton *b : pages[i].body->findChildren<QAbstractButton *>()) in = in || b->text().toLower().contains(q);
            list->item(i)->setHidden(!in);
            if (in && first < 0) first = i;
        }
        if (first < 0) {
            title->clear();
            stack->setCurrentWidget(empty);
        } else if (list->currentRow() < 0 || list->item(list->currentRow())->isHidden()) {
            list->setCurrentRow(first);
        }
    });
    list->setCurrentRow(0);
    *search = field;
    return body;
}

/** OK, Cancel and Apply: Apply is on only if something changed; OK applies what changed and closes. */
void buttons(FramedDialog &d, Model *model, const Choices &c) {
    auto saved = std::make_shared<QString>(c.state());
    QPushButton *ok = d.addButton("OK");
    ok->setProperty("kind", "primary");
    QPushButton *cancel = d.addButton(T("common.cancel"));
    QPushButton *apply = d.addButton(T("settings.apply"));
    auto compare = [=] { apply->setEnabled(c.state() != *saved); };
    QObject::connect(c.language, &QComboBox::currentIndexChanged, &d, compare);
    QObject::connect(c.probe, &QComboBox::currentIndexChanged, &d, compare);
    for (QCheckBox *box : {c.types, c.lan, c.automatic, c.splash}) QObject::connect(box, &QCheckBox::toggled, &d, compare);
    compare();
    auto save = [=] {
        model->request("prefs.put", {{"node", "ui"}, {"values", QJsonObject{{"language", Choices::CODES[c.language->currentIndex()]},
                                                                            {"terminalCommands", c.types->isChecked() ? "true" : "false"},
                                                                            {"splash", c.splash->isChecked() ? "true" : "false"}}}});
        Splash::remember(c.splash->isChecked());
        model->setTypesCommands(c.types->isChecked());
        model->request("monitor.probeMinutes", {{"minutes", Choices::PROBE_MINUTES[c.probe->currentIndex()]}});
        if (c.lan->isChecked() != model->lanOn()) model->request("lan.on", {{"on", c.lan->isChecked()}});
        if (c.carrierGrade->isChecked() != model->lanCarrierGrade())
            model->request("lan.carrierGrade", {{"on", c.carrierGrade->isChecked()}}, [model](const QJsonValue &) { model->refreshTeam(); });
        model->request("update.automatic", {{"on", c.automatic->isChecked()}});
        *saved = c.state();
        compare();
    };
    QObject::connect(apply, &QPushButton::clicked, &d, save);
    QObject::connect(ok, &QPushButton::clicked, &d, [&d, save, apply] {
        if (apply->isEnabled()) save();
        d.accept();
    });
    QObject::connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
}
}  // namespace

void SettingsDialog::open(Model *model, Actions *actions, QWidget *parent) {
    // The settings and the update state first: the pages open with them.
    model->request("prefs.get", {{"node", "ui"}}, [=](const QJsonValue &pv) {
        QJsonObject ui = pv.toObject();
        model->request("update.state", {}, [=](const QJsonValue &uv) {
            FramedDialog d(parent, T("activity.settings"));
            d.body()->setContentsMargins(0, 0, 0, 0);
            Choices c;
            QList<Page> pages{{T("settings.appearance"), "moon", appearancePage(ui, c)},
                              {T("settings.vault"), "lock", vaultPage(model, actions, d)},
                              {T("settings.backup"), "save", backupPage(model, actions, parent)},
                              {T("settings.servers"), "shield", serversPage(ui, c)},
                              {T("settings.lan"), "network", networkPage(model, c)},
                              {T("settings.updates"), "download", updatesPage(model, parent, uv.toObject(), c, d)},
                              {T("settings.about"), "note", aboutPage(model)}};
            QLineEdit *search = nullptr;
            d.body()->addWidget(sections(pages, d, &search));
            d.resize(900, 640);
            buttons(d, model, c);
            search->setFocus();
            d.exec();
        });
    });
}
