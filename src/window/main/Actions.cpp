#include "window/main/Actions.h"

#include "app/core/Backend.h"
#include "app/core/Model.h"
#include "app/tools/Texts.h"
#include "dialogs/Team.h"
#include "terminal/TerminalView.h"
#include "widgets/parts/Chrome.h"
#include "widgets/look/Icons.h"
#include "widgets/look/Theme.h"
#include "widgets/parts/Ui.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontMetrics>
#include <QFormLayout>
#include <QGridLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QTreeWidget>
#include <QMap>
#include <QMenu>
#include <QPointer>
#include <QRegularExpression>
#include <QShortcut>
#include <QSpinBox>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <memory>

namespace {
QString parentOf(const QString &p) {
    int i = p.lastIndexOf('/');
    return i < 0 ? QString() : p.left(i);
}

QString nameOf(const QString &p) {
    return p.mid(p.lastIndexOf('/') + 1);
}

QString merge(const QString &parent, const QString &name) {
    return parent.isEmpty() ? name : parent + "/" + name;
}

bool inside(const QString &p, const QString &folder) {
    return folder.isEmpty() || p == folder || p.startsWith(folder + "/");
}

QString label(const QString &folder) {
    return folder.isEmpty() ? T("tree.datacenter") : folder;
}

QAction *item(QMenu *m, const QString &text, const QString &icon, std::function<void()> act) {
    QAction *a = m->addAction(Icons::icon(icon, 13, Theme::text), text);
    QObject::connect(a, &QAction::triggered, m, [act] { act(); });
    return a;
}

/** The commands over a terminal: icons on its dark patch, brighter under the mouse. */
IconButton *terminalAction(const QString &icon, const QString &tip, bool danger, std::function<void()> act) {
    auto *b = new IconButton(icon, 13, QSize(20, 18), tip);
    b->colour = danger ? Theme::danger : Theme::actionIcon;
    b->hoverColour = danger ? Theme::danger : Theme::actionHover;
    b->hoverBackground = Theme::hover;
    QObject::connect(b, &QAbstractButton::clicked, b, [act] { act(); });
    return b;
}
/** Widest the server's name gets in the badge, in pixels. */
constexpr int BADGE_WIDTH = 180;
/** Whose terminal this is, before its commands: a green terminal icon and the server's name (the shell's for this computer). */
QWidget *hostBadge(const QString &name) {
    auto *w = new QWidget;
    w->setToolTip(name);
    auto *row = new QHBoxLayout(w);
    row->setContentsMargins(2, 0, 6, 0);
    row->setSpacing(4);
    auto *icon = new QLabel;
    Icons::show(icon, "terminal", 13, Theme::ok);
    // A long name is cut with an ellipsis, so it does not push the commands away; the tooltip keeps it whole.
    QFont font("DejaVu Sans Mono");
    font.setPixelSize(12);
    auto *text = new QLabel(QFontMetrics(font).elidedText(name, Qt::ElideRight, BADGE_WIDTH));
    text->setFont(font);
    Theme::style(text, "color: @actionIcon;");
    row->addWidget(icon);
    row->addWidget(text);
    return w;
}
}  // namespace

Actions::Actions(Model *model, QWidget *window) : QObject(window), model(model), window(window) {}

// ── Hosts ──

void Actions::newHost(const QString &folder) {
    hostDialog(QString(), {{"group", folder}, {"port", 22}, {"user", "root"}, {"authMethod", "KEY"}}, false);
}

void Actions::editHost(const QString &id) {
    if (const Host *h = model->host(id)) hostDialog(id, h->json, false);
}

void Actions::duplicateHost(const QString &id) {
    const Host *h = model->host(id);
    if (!h) return;
    QJsonObject c = h->json;
    c.remove("id");
    c["name"] = T("hostForm.copyName", h->name);
    c["note"] = "";
    c["state"] = "UNKNOWN";
    hostDialog(QString(), c, true);
}

namespace {
/** Each hop: another host of the list by name, or [user@]host[:port]; never the host itself. Empty if all are fine. */
QString jumpProblem(const QString &jumps, const QString &name, const QStringList &others) {
    static const QRegularExpression destination("^([^@\\s]+@)?[^@:\\s/]+(:\\d{1,5})?$");
    QString why;
    for (QString t : jumps.split(',', Qt::SkipEmptyParts)) {
        t = t.trimmed();
        if (t.isEmpty()) continue;
        if (!name.isEmpty() && t.compare(name, Qt::CaseInsensitive) == 0) why = t + ": " + T("host.jump") + " = " + name;
        else if (!others.contains(t, Qt::CaseInsensitive) && !destination.match(t).hasMatch()) why = t + "?";
    }
    return why;
}

/** The fields of a host, in a grid, with their checks. */
class HostForm {
public:
    static inline const QStringList METHODS{"KEY", "PASSWORD", "AGENT"};

    HostForm(const QJsonObject &h, const Model *model, const QString &existingId, bool existing) : model(model), existingId(existingId) {
        grid = new QGridLayout;
        grid->setHorizontalSpacing(8);
        grid->setVerticalSpacing(8);
        grid->setColumnMinimumWidth(0, 150);
        grid->setColumnMinimumWidth(1, 300);
        name = new QLineEdit(h["name"].toString());
        name->setPlaceholderText("web-03");
        name->setMaxLength(64);
        nameError = warning();
        add(T("host.name"), withWarning(name, nameError));
        group = new QComboBox;
        group->setEditable(true);
        group->addItems(model->folders());
        group->setEditText(h["group"].toString());
        group->lineEdit()->setPlaceholderText(T("hostForm.folder.prompt"));
        add(T("host.folder"), group);
        address = new QLineEdit(h["address"].toString());
        address->setPlaceholderText("203.0.113.10");
        add(T("host.address"), address);
        port = new QSpinBox;
        port->setRange(1, 65535);
        port->setValue(h["port"].toInt(22));
        port->setButtonSymbols(QAbstractSpinBox::NoButtons);
        add(T("host.port"), port);
        user = new QLineEdit(h["user"].toString());
        add(T("host.user"), user);
        auth = new QComboBox;
        auth->addItems({T("auth.key"), T("auth.password"), T("auth.agent")});
        auth->setCurrentIndex(std::max(0, int(METHODS.indexOf(h["authMethod"].toString("KEY")))));
        add(T("host.auth"), auth);
        key = new QLineEdit(h["key"].isString() ? h["key"].toString() : "id_ed25519");
        keyCaption = add(T("hostForm.key"), key);
        password = new QLineEdit;
        Ui::revealable(password);
        password->setPlaceholderText(existing ? T("hostForm.password.keep") : T("hostForm.password.ask"));
        passwordCaption = add(T("auth.password"), password);
        jump = new QComboBox;
        jump->setEditable(true);
        for (const Host &x : model->hosts())
            if (x.id != existingId) others << x.name;
        others.sort(Qt::CaseInsensitive);
        jump->addItems(others);
        jump->setEditText(h["jump"].toString());
        jump->lineEdit()->setPlaceholderText(T("hostForm.jump.prompt"));
        jumpError = warning();
        add(T("host.jump"), withWarning(jump, jumpError));
        os = new QLineEdit(h["os"].toString());
        os->setPlaceholderText("Ubuntu 24.04 LTS");
        add(T("host.os"), os);
        grid->addWidget(Ui::faint(T("hostForm.hint")), row++, 0, 1, 2);
    }

    /** Shows what the method needs and why it cannot be saved yet; true if it can. */
    bool check() {
        bool k = auth->currentIndex() == 0, p = auth->currentIndex() == 1;
        keyCaption->setVisible(k);
        key->setVisible(k);
        passwordCaption->setVisible(p);
        password->setVisible(p);
        QString n = name->text().trimmed();
        bool taken = std::any_of(model->hosts().begin(), model->hosts().end(),
                                 [&](const Host &x) { return x.id != existingId && n.compare(x.name, Qt::CaseInsensitive) == 0; });
        nameError->setText(taken ? T("hostForm.nameTaken") : QString());
        nameError->setVisible(taken);
        QString jumpWhy = jumpProblem(jump->currentText(), n, others);
        jumpError->setText(jumpWhy);
        jumpError->setVisible(!jumpWhy.isEmpty());
        return !n.isEmpty() && !address->text().trimmed().isEmpty() && !user->text().trimmed().isEmpty() && !taken && jumpWhy.isEmpty();
    }

    /** `h` with what was typed. */
    QJsonObject read(QJsonObject h) const {
        if (!existingId.isEmpty()) h["id"] = existingId;
        h["name"] = name->text().trimmed();
        h["group"] = group->currentText().trimmed();
        h["address"] = address->text().trimmed();
        h["port"] = port->value();
        h["user"] = user->text().trimmed();
        h["authMethod"] = METHODS[auth->currentIndex()];
        h["key"] = auth->currentIndex() == 0 ? QJsonValue(key->text().trimmed()) : QJsonValue();
        QString j = jump->currentText().trimmed();
        h["jump"] = j.isEmpty() ? QJsonValue() : QJsonValue(j);
        h["os"] = os->text().trimmed().isEmpty() ? QJsonValue() : QJsonValue(os->text().trimmed());
        if (!h.contains("state")) h["state"] = "UNKNOWN";
        return h;
    }
    /** The password typed, for the vault; null unless the method is the password. */
    QString typedPassword() const { return auth->currentIndex() == 1 ? password->text() : QString(); }

    QGridLayout *grid;
    QLineEdit *name, *address, *user;
    QComboBox *auth, *jump;

private:
    static QLabel *caption(const QString &t) {
        auto *l = new QLabel(t);
        Theme::style(l, "color: @faint;");
        return l;
    }
    static QLabel *warning() {
        auto *l = new QLabel;
        Theme::style(l, "color: @warn;");
        l->setWordWrap(true);
        l->hide();
        return l;
    }
    static QLayout *withWarning(QWidget *field, QLabel *why) {
        auto *box = new QVBoxLayout;
        box->setSpacing(4);
        box->addWidget(field);
        box->addWidget(why);
        return box;
    }
    QLabel *add(const QString &label, QWidget *field) {
        QLabel *c = caption(label);
        grid->addWidget(c, row, 0);
        grid->addWidget(field, row++, 1);
        return c;
    }
    QLabel *add(const QString &label, QLayout *field) {
        QLabel *c = caption(label);
        grid->addWidget(c, row, 0);
        grid->addLayout(field, row++, 1);
        return c;
    }

    const Model *model;
    QString existingId;
    QStringList others;
    int row = 0;
    QComboBox *group;
    QSpinBox *port;
    QLineEdit *key, *password, *os;
    QLabel *nameError, *jumpError, *keyCaption, *passwordCaption;
};
}  // namespace

void Actions::hostDialog(const QString &existingId, QJsonObject h, bool duplicate) {
    const Host *existing = existingId.isEmpty() ? nullptr : model->host(existingId);
    FramedDialog d(window, existing ? T("hostForm.title.edit", existing->name) : duplicate ? T("hostForm.title.duplicate") : T("hostForm.title.add"));
    HostForm form(h, model, existingId, existing);
    d.body()->addLayout(form.grid);
    QPushButton *ok = d.buttons(existing ? T("common.save") : T("common.add"));
    auto refresh = [&] {
        ok->setEnabled(form.check());
        d.adjustSize();
    };
    for (QLineEdit *f : {form.name, form.address, form.user}) connect(f, &QLineEdit::textChanged, &d, refresh);
    connect(form.auth, &QComboBox::currentIndexChanged, &d, refresh);
    connect(form.jump, &QComboBox::editTextChanged, &d, refresh);
    refresh();
    form.name->setFocus();
    if (d.exec() != QDialog::Accepted) return;

    QString typed = form.typedPassword();
    bool isNew = existingId.isEmpty();
    model->request("host.save", {{"host", form.read(h)}}, [this, typed, isNew](const QJsonValue &saved) {
        QString id = saved.toObject()["id"].toString();
        // The password goes into the vault, never into hosts.json.
        if (!typed.isEmpty()) unlockVault([this, id, typed] { model->request("vault.remember", {{"host", id}, {"password", typed}}); });
        if (isNew) model->select(Position::HostItem, id);
    });
}

void Actions::moveHost(const QString &id) {
    const Host *h = model->host(id);
    if (!h) return;
    QStringList choices{""};
    choices << model->folders();
    if (auto dest = Ui::folder(window, T("hosts.move.title"), T("folders.move.label", h->name), choices, h->group))
        model->request("host.move", {{"host", id}, {"folder", *dest}});
}

void Actions::removeHost(const QString &id) {
    const Host *h = model->host(id);
    if (!h) return;
    if (model->connected(id) > 0) {
        Ui::warning(window, T("hosts.remove.title"), T("hosts.remove.busy", h->name));
        return;
    }
    if (Ui::confirm(window, T("hosts.remove.title"), T("hosts.remove.ask", h->name, h->address), T("common.remove"), true))
        model->request("host.remove", {{"host", id}});
}

void Actions::sendHost(const QString &id) {
    TeamDialogs::send(model, this, window, id);
}

void Actions::checkDisks(const QStringList &ids) {
    bool password = std::any_of(ids.begin(), ids.end(), [this](const QString &id) {
        const Host *h = model->host(id);
        return h && h->auth == Host::Auth::Password;
    });
    auto read = [this, ids] {
        for (const QString &id : ids) model->request("metrics.refresh", {{"host", id}, {"force", true}});
    };
    if (password && model->vaultExists() && !model->vaultOpen()) unlockVault(read);
    else read();
}

void Actions::sshImport(const QString &folder) {
    model->request("sshconfig.read", {}, [this, folder](const QJsonValue &v) {
        QJsonObject r = v.toObject();
        QJsonArray entries = r["entries"].toArray();
        QString file = r["file"].toString().replace(QDir::homePath(), "~");
        if (entries.isEmpty()) {
            Ui::warning(window, T("sshImport.title"), T("sshImport.none", file));
            return;
        }
        FramedDialog d(window, T("sshImport.title"));
        d.body()->addWidget(new QLabel(T("sshImport.found", QString::number(entries.size()), file)));
        auto *table = new QTableWidget(int(entries.size()), 5);
        table->setHorizontalHeaderLabels({"", T("host.name"), T("sshImport.col.destination"), T("sshImport.col.key"), T("sshImport.col.note")});
        table->verticalHeader()->hide();
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->horizontalHeader()->setStretchLastSection(true);
        QList<QCheckBox *> boxes;
        for (int i = 0; i < entries.size(); i++) {
            QJsonObject e = entries[i].toObject();
            auto *c = new QCheckBox;
            c->setChecked(e["chosen"].toBool());
            boxes << c;
            auto *cell = new QWidget;
            auto *l = new QHBoxLayout(cell);
            l->setContentsMargins(0, 0, 0, 0);
            l->setAlignment(Qt::AlignCenter);
            l->addWidget(c);
            table->setCellWidget(i, 0, cell);
            auto text = [&](int col, const QString &t, bool mono = false) {
                auto *it = new QTableWidgetItem(t.isEmpty() ? "-" : t);
                it->setTextAlignment(Qt::AlignCenter);
                if (mono) it->setFont(QFont("DejaVu Sans Mono", 9));
                if (col == 4 || t.isEmpty()) it->setForeground(Theme::faint);
                it->setToolTip(t);
                table->setItem(i, col, it);
            };
            text(1, e["alias"].toString());
            text(2, e["destination"].toString(), true);
            text(3, e["identityFile"].toString().replace(QDir::homePath(), "~"), true);
            text(4, e["why"].toString());
        }
        table->setColumnWidth(0, 36);
        table->setColumnWidth(1, 130);
        table->setColumnWidth(2, 230);
        table->setColumnWidth(3, 150);
        table->setMinimumSize(800, std::min(10, int(entries.size())) * 30 + 34);
        d.body()->addWidget(table);
        auto *where = new QComboBox;
        where->setEditable(true);
        where->addItem(T("tree.datacenter"), "");
        for (const QString &f : model->folders()) where->addItem(f, f);
        where->setEditText(folder.isEmpty() ? T("tree.datacenter") : folder);
        auto *row = new QHBoxLayout;
        row->addWidget(new QLabel(T("sshImport.folder")));
        row->addWidget(where, 1);
        d.body()->addLayout(row);
        QPushButton *ok = d.buttons(T("sshImport.import"));
        auto refresh = [&] { ok->setEnabled(std::any_of(boxes.begin(), boxes.end(), [](QCheckBox *c) { return c->isChecked(); })); };
        for (QCheckBox *c : boxes) connect(c, &QCheckBox::toggled, &d, refresh);
        refresh();
        if (d.exec() != QDialog::Accepted) return;
        QJsonArray aliases;
        for (int i = 0; i < entries.size(); i++)
            if (boxes[i]->isChecked()) aliases.append(entries[i].toObject()["alias"]);
        QString dest = where->currentText() == T("tree.datacenter") ? QString() : where->currentText();
        model->request("sshconfig.import", {{"aliases", aliases}, {"folder", dest}});
    });
}

// ── Folders ──

QString Actions::folderNameError(const QString &parent, const QString &name, const QString &excluded) const {
    if (name.isEmpty()) return T("folders.enterName");
    if (name.contains('/')) return T("folders.noSlash");
    QString p = merge(parent, name);
    if (p.compare(excluded, Qt::CaseInsensitive) != 0 && model->folderExists(p)) return T("folders.exists");
    return QString();
}

void Actions::newFolder(const QString &parent) {
    QString text = parent.isEmpty() ? T("folders.nameInDatacenter") : T("folders.nameIn", parent);
    auto n = Ui::name(window, T("folders.new"), text, "", T("vault.create.button"),
                      [this, parent](const QString &x) { return folderNameError(parent, x, QString()); });
    if (!n) return;
    QString p = merge(parent, *n);
    model->request("folder.create", {{"path", p}}, [this, p](const QJsonValue &) { model->select(Position::Folder, p); });
}

void Actions::renameFolder(const QString &path) {
    QString parent = parentOf(path);
    auto n = Ui::name(window, T("folders.rename.title"), T("folders.rename.label", path), nameOf(path), T("folders.rename"),
                      [this, parent, path](const QString &x) { return folderNameError(parent, x, path); });
    if (n) model->request("folder.move", {{"from", path}, {"to", merge(parent, *n)}});
}

void Actions::moveFolder(const QString &path) {
    QStringList choices{""};
    for (const QString &f : model->folders())
        if (!inside(f, path)) choices << f;
    if (auto dest = Ui::folder(window, T("folders.move.title"), T("folders.move.label", path), choices, parentOf(path))) moveFolderTo(path, *dest);
}

void Actions::moveFolderTo(const QString &path, const QString &dest) {
    if (dest == parentOf(path) || inside(dest, path)) return;
    QString fresh = merge(dest, nameOf(path));
    if (model->folderExists(fresh)
        && !Ui::confirm(window, T("folders.move.title"), T("folders.merge", label(dest), nameOf(path)), T("folders.merge.button")))
        return;
    model->request("folder.move", {{"from", path}, {"to", fresh}});
}

void Actions::deleteFolder(const QString &path) {
    int n = int(std::count_if(model->hosts().begin(), model->hosts().end(), [&](const Host &h) { return inside(h.group, path); }));
    QString on = label(parentOf(path));
    QString text = n == 0 ? T("folders.delete.empty", path) : n == 1 ? T("folders.delete.one", path, on) : T("folders.delete.many", path, n, on);
    if (Ui::confirm(window, T("folders.delete.title"), text, T("common.delete"), true)) model->request("folder.delete", {{"path", path}});
}

// ── Vault ──

void Actions::unlockVault(std::function<void()> then) {
    if (model->vaultOpen()) {
        if (then) then();
        return;
    }
    bool create = !model->vaultExists();
    FramedDialog d(window, create ? T("vault.create.title") : T("vault.unlock.title"));
    if (create) {
        auto *l = new QLabel(T("vault.create.choose"));
        l->setWordWrap(true);
        d.body()->addWidget(l);
    }
    d.body()->addWidget(new QLabel(T("vault.master")));
    auto *field = new QLineEdit;
    Ui::revealable(field);
    field->setMinimumWidth(380);
    d.body()->addWidget(field);
    QLineEdit *again = nullptr;
    if (create) {
        d.body()->addWidget(new QLabel(T("vault.repeat")));
        again = new QLineEdit;
        Ui::revealable(again);
        d.body()->addWidget(again);
    }
    QLabel *note = Ui::faint(T(create ? "vault.create.note" : "vault.unlock.note"));
    note->setWordWrap(true);
    d.body()->addWidget(note);
    auto *error = new QLabel;
    Theme::style(error, "color: @warn;");
    d.body()->addWidget(error);
    QPushButton *ok = d.addButton(T("common.cancel"));
    connect(ok, &QPushButton::clicked, &d, &QDialog::reject);
    ok = d.addButton(T(create ? "vault.create.button" : "vault.unlock.button"));
    ok->setProperty("kind", "primary");
    ok->setDefault(true);
    // The dialog stays open on a wrong password: the backend answers, then it closes or says why. An answer that
    // arrives after the dialog has gone does nothing.
    auto alive = std::make_shared<bool>(true);
    connect(ok, &QPushButton::clicked, &d, [&, alive] {
        if (field->text().isEmpty()) {
            error->setText(T("vault.enterPassword"));
            return;
        }
        if (again && again->text() != field->text()) {
            error->setText(T("vault.mismatch"));
            return;
        }
        ok->setEnabled(false);
        model->request(create ? "vault.create" : "vault.open", {{"password", field->text()}}, [&, alive](const QJsonValue &v) {
            if (!*alive) return;
            ok->setEnabled(true);
            QJsonObject r = v.toObject();
            model->setVault(r["exists"].toBool(), r["open"].toBool());
            if (model->vaultOpen()) d.accept();
            else {
                error->setText(T("vault.wrong"));
                field->selectAll();
                field->setFocus();
            }
        }, [&, alive](const QString &why) {
            if (!*alive) return;
            ok->setEnabled(true);
            error->setText(why);
        });
    });
    field->setFocus();
    bool accepted = d.exec() == QDialog::Accepted;
    *alive = false;
    if (accepted && then) then();
}

void Actions::toggleVault() {
    if (model->vaultOpen()) model->request("vault.lock");
    else unlockVault();
}

void Actions::changeVaultPassword() {
    unlockVault([this] {
        FramedDialog d(window, T("vault.change.title"));
        auto field = [&](const QString &caption) {
            d.body()->addWidget(new QLabel(caption));
            auto *f = new QLineEdit;
            Ui::revealable(f);
            f->setMinimumWidth(380);
            d.body()->addWidget(f);
            return f;
        };
        QLineEdit *current = field(T("vault.change.current")), *fresh = field(T("vault.change.new")), *again = field(T("vault.repeat"));
        QLabel *note = Ui::faint(T("vault.change.note"));
        note->setWordWrap(true);
        d.body()->addWidget(note);
        auto *error = new QLabel;
        Theme::style(error, "color: @warn;");
        d.body()->addWidget(error);
        QPushButton *cancel = d.addButton(T("common.cancel"));
        connect(cancel, &QPushButton::clicked, &d, &QDialog::reject);
        QPushButton *ok = d.addButton(T("vault.change.button"));
        ok->setProperty("kind", "primary");
        auto alive = std::make_shared<bool>(true);
        connect(ok, &QPushButton::clicked, &d, [&, alive] {
            if (current->text().isEmpty() || fresh->text().isEmpty() || again->text().isEmpty()) return error->setText(T("vault.fillAll"));
            if (fresh->text() != again->text()) return error->setText(T("vault.newMismatch"));
            model->request("vault.change", {{"current", current->text()}, {"password", fresh->text()}}, [&, alive](const QJsonValue &v) {
                if (!*alive) return;
                if (v.toObject()["ok"].toBool()) d.accept();
                else error->setText(T("vault.currentWrong"));
            }, [&, alive](const QString &why) {
                if (*alive) error->setText(T("common.error", why));
            });
        });
        current->setFocus();
        bool accepted = d.exec() == QDialog::Accepted;
        *alive = false;
        if (accepted) Ui::warning(window, T("vault.change.title"), T("vault.changed"));
    });
}

// ── Menus ──

QMenu *Actions::hostMenu(const QString &id) {
    auto *m = new QMenu(window);
    item(m, T("hosts.openConsole"), "terminal", [this, id] { model->showOrOpen(id); });
    item(m, T("hosts.disconnect"), "power", [this, id] { model->disconnectHost(id); })->setEnabled(model->connected(id) > 0);
    m->addSeparator();
    item(m, T("hosts.editMenu"), "edit", [this, id] { editHost(id); });
    item(m, T("hosts.duplicate"), "plus", [this, id] { duplicateHost(id); });
    item(m, T("common.moveTo"), "right", [this, id] { moveHost(id); });
    item(m, T("hosts.sendTeam"), "share", [this, id] { sendHost(id); });
    m->addSeparator();
    item(m, T("common.remove"), "trash", [this, id] { removeHost(id); });
    return m;
}

QMenu *Actions::recentMenu() {
    auto *m = new QMenu(window);
    // The heading, as a line that cannot be chosen (the style draws sections without their text).
    m->addAction(Icons::icon("history", 13, Theme::faint), T("recent.title") + "   Ctrl+Shift+O")->setEnabled(false);
    m->addSeparator();
    QStringList ids = model->recentHosts();
    for (int i = 0; i < ids.size(); i++) {
        const Host *h = model->host(ids[i]);
        QString id = ids[i], name = h->name;
        // The number is the key; after the tab, the destination on the right.
        item(m, "&" + QString::number(i + 1) + "  " + name.replace('&', "&&") + "\t" + h->destination(), "server",
             [this, id] { model->showOrOpen(id); });
    }
    if (ids.isEmpty()) m->addAction(T("recent.none"))->setEnabled(false);

    // The launch groups: each opens its servers split in a tab.
    m->addSeparator();
    m->addAction(Icons::icon("grid", 13, Theme::faint), T("launch.title"))->setEnabled(false);
    m->addSeparator();
    const QList<LaunchGroup> &groups = model->launchGroups();
    for (const LaunchGroup &g : groups) {
        QStringList hosts = g.hosts;
        QString name = g.name;
        item(m, name.replace('&', "&&") + "\t" + T("launch.count", QString::number(hosts.size())), "grid",
             [this, hosts] { model->openSplit(hosts); });
    }
    item(m, T("launch.new"), "plus", [this] { editLaunch(-1); });
    if (!groups.isEmpty()) {
        QMenu *edit = m->addMenu(Icons::icon("edit", 13, Theme::text), T("launch.edit"));
        QMenu *drop = m->addMenu(Icons::icon("trash", 13, Theme::text), T("launch.delete"));
        for (int i = 0; i < groups.size(); i++) {
            QString name = groups[i].name;
            name.replace('&', "&&");
            edit->addAction(name, this, [this, i] { editLaunch(i); });
            drop->addAction(name, this, [this, i] { deleteLaunch(i); });
        }
    }
    return m;
}

void Actions::editLaunch(int index) {
    QList<LaunchGroup> groups = model->launchGroups();
    if (index >= groups.size()) return;
    LaunchGroup g = index < 0 ? LaunchGroup{} : groups[index];
    FramedDialog d(window, T(index < 0 ? "launch.dialog.new" : "launch.dialog.edit"));
    d.body()->addWidget(new QLabel(T("launch.name")));
    auto *name = new QLineEdit(g.name);
    name->setMinimumWidth(420);
    d.body()->addWidget(name);
    d.body()->addWidget(new QLabel(T("launch.hosts")));
    // The servers of the group first, in their order and ticked; then the others. Dragged, they change order.
    // In columns: name, folder, destination.
    auto *list = new QTreeWidget;
    list->setColumnCount(3);
    list->setHeaderLabels({T("launch.col.name"), T("launch.col.folder"), T("launch.col.address")});
    list->setRootIsDecorated(false);
    list->setUniformRowHeights(true);
    list->setDragDropMode(QAbstractItemView::InternalMove);
    list->setSelectionMode(QAbstractItemView::SingleSelection);
    list->invisibleRootItem()->setFlags(list->invisibleRootItem()->flags() | Qt::ItemIsDropEnabled);
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(2, QHeaderView::Stretch);
    list->setStyleSheet("QTreeWidget::item { padding: 4px 0; }");
    list->setMinimumSize(640, 300);
    auto add = [list](const Host &h, bool on) {
        auto *i = new QTreeWidgetItem(list, {h.name, h.group.isEmpty() ? T("tree.datacenter") : h.group, h.destination()});
        i->setIcon(0, Icons::icon("server", 14, Theme::ok));
        i->setData(0, Qt::UserRole, h.id);
        i->setFlags((i->flags() | Qt::ItemIsUserCheckable | Qt::ItemIsDragEnabled) & ~Qt::ItemIsDropEnabled);
        i->setCheckState(0, on ? Qt::Checked : Qt::Unchecked);
    };
    for (const QString &id : g.hosts)
        if (const Host *h = model->host(id)) add(*h, true);
    for (const Host &h : model->hosts())
        if (h.id != Host::LOCAL && !g.hosts.contains(h.id)) add(h, false);
    // Each column as wide as its longest text, plus some air.
    for (int c = 0; c < 2; c++) {
        list->resizeColumnToContents(c);
        list->setColumnWidth(c, list->columnWidth(c) + 36);
    }
    d.body()->addWidget(list);
    auto *error = new QLabel;
    Theme::style(error, "color: @warn;");
    d.body()->addWidget(error);
    QPushButton *ok = d.buttons(T("launch.save"));
    auto chosen = [list] {
        QStringList ids;
        for (int i = 0; i < list->topLevelItemCount(); i++)
            if (list->topLevelItem(i)->checkState(0) == Qt::Checked) ids << list->topLevelItem(i)->data(0, Qt::UserRole).toString();
        return ids;
    };
    auto refresh = [&] {
        QString why = name->text().trimmed().isEmpty() ? T("launch.noName") : chosen().isEmpty() ? T("launch.noHosts") : QString();
        error->setText(why);
        ok->setEnabled(why.isEmpty());
    };
    QObject::connect(name, &QLineEdit::textChanged, &d, refresh);
    QObject::connect(list, &QTreeWidget::itemChanged, &d, refresh);
    refresh();
    name->setFocus();
    if (d.exec() != QDialog::Accepted) return;
    g.name = name->text().trimmed();
    g.hosts = chosen();
    if (index < 0) groups << g;
    else groups[index] = g;
    model->setLaunchGroups(groups);
}

void Actions::deleteLaunch(int index) {
    QList<LaunchGroup> groups = model->launchGroups();
    if (index < 0 || index >= groups.size()) return;
    if (!Ui::confirm(window, T("launch.delete"), T("launch.deleteAsk", groups[index].name), T("launch.delete"), true)) return;
    groups.removeAt(index);
    model->setLaunchGroups(groups);
}

void Actions::recentShortcut(QWidget *on) {
    auto *key = new QShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_O), on);
    key->setContext(Qt::WindowShortcut);
    QObject::connect(key, &QShortcut::activated, on, [this, on] {
        // Not over a dialog: its window, not the terminal, has the keyboard.
        if (QApplication::activeModalWidget()) return;
        QMenu *menu = recentMenu();
        menu->adjustSize();
        menu->exec(on->mapToGlobal(QPoint((on->width() - menu->width()) / 2, on->height() / 4)));
        menu->deleteLater();
    });
}

void Actions::recentBeside(QWidget *button) {
    QMenu *menu = recentMenu();
    menu->exec(button->mapToGlobal(QPoint(button->width(), 0)));
    menu->deleteLater();
}

QMenu *Actions::folderMenu(const QString &path) {
    auto *m = new QMenu(window);
    item(m, T("hosts.addHere"), "plus", [this, path] { newHost(path); });
    item(m, T("folders.newSub"), "folder", [this, path] { newFolder(path); });
    item(m, T("sshImport.menu"), "download", [this, path] { sshImport(path); });
    // Its servers (those of its subfolders too), PER_TAB to a tab, split as a grid.
    QStringList ids;
    for (const Host &h : model->hosts())
        if (inside(h.group, path)) ids << h.id;
    item(m, T("folders.openSplit"), "grid", [this, ids] { model->openSplit(ids); })->setEnabled(!ids.isEmpty());
    m->addSeparator();
    item(m, T("folders.renameMenu"), "edit", [this, path] { renameFolder(path); });
    item(m, T("common.moveTo"), "right", [this, path] { moveFolder(path); });
    m->addSeparator();
    item(m, T("common.delete"), "trash", [this, path] { deleteFolder(path); });
    return m;
}

QMenu *Actions::datacenterMenu() {
    auto *m = new QMenu(window);
    item(m, T("hosts.add"), "plus", [this] { newHost(""); });
    item(m, T("folders.new"), "folder", [this] { newFolder(""); });
    item(m, T("sshImport.menu"), "download", [this] { sshImport(""); });
    m->addSeparator();
    item(m, T("hosts.checkReachability"), "ok", [this] { model->request("monitor.tryAll"); });
    return m;
}

QMenu *Actions::terminalMenu(Session *s) {
    auto *m = new QMenu(window);
    TerminalView *t = s->view;
    item(m, T("terminal.copy"), "copy", [t] { t->copySelection(); })->setEnabled(t->hasSelection());
    item(m, T("terminal.paste"), "paste", [t] { t->pasteClipboard(); })->setEnabled(!QApplication::clipboard()->text().isEmpty());
    m->addSeparator();
    for (bool across : {true, false}) {
        QMenu *sub = m->addMenu(Icons::icon(across ? "split-right" : "split-down", 13, Theme::text), T(across ? "terminal.splitRight" : "terminal.splitDown"));
        // The local shell first, then the same host, then every other host by folder.
        if (s->hostId != Host::LOCAL)
            item(sub, T("terminal.splitLocal", model->localHost().name), "terminal", [this, s, across] { model->split(s, across, Host::LOCAL); });
        const Host *own = model->host(s->hostId);
        item(sub, T("terminal.splitSameHost", own ? own->name : QString()), "terminal", [this, s, across] { model->split(s, across); });
        sub->addSeparator();
        QList<const Host *> sorted;
        for (const Host &h : model->hosts())
            if (h.id != s->hostId) sorted << &h;
        std::sort(sorted.begin(), sorted.end(), [](const Host *a, const Host *b) { return a->name.compare(b->name, Qt::CaseInsensitive) < 0; });
        QMap<QString, QMenu *> folders;
        QList<const Host *> loose;
        for (const Host *h : sorted) {
            if (h->group.isEmpty()) loose << h;
            else if (!folders.contains(h->group.toLower())) folders.insert(h->group.toLower(), nullptr);
        }
        for (auto it = folders.begin(); it != folders.end(); ++it) {
            QString g;
            for (const Host *h : sorted)
                if (h->group.toLower() == it.key()) g = h->group;
            it.value() = sub->addMenu(Icons::icon("folder", 13, Theme::text), g);
        }
        for (const Host *h : sorted) {
            QString id = h->id;
            QMenu *where = h->group.isEmpty() ? sub : folders.value(h->group.toLower());
            item(where, h->name, "server", [this, s, across, id] { model->split(s, across, id); });
        }
    }
    // Uploads go over SFTP: not in a local shell, where the files are already here.
    if (s->hostId != Host::LOCAL) {
        m->addSeparator();
        item(m, T("terminal.upload"), "upload", [this, s] { upload(s, {}); })->setEnabled(s->alive());
    }
    // On a name listed by ls: download it (a file, or a folder with what is inside).
    QString name = t->menuName(), command = t->menuCommand();
    if (s->hostId != Host::LOCAL && !name.isEmpty())
        item(m, T("terminal.download", name), "download", [this, s, name, command] { download(s, name, command); })->setEnabled(s->alive());
    if (s->tab()->panes().size() > 1) {
        m->addSeparator();
        item(m, T("terminal.closePane"), "close", [this, s] { model->closePane(s); });
    }
    return m;
}

QList<QWidget *> Actions::hostButtons(const QString &id) {
    const Host *h = model->host(id);
    if (!h) return {};
    int n = model->connected(id);
    QPushButton *disconnect = Ui::button(T("hosts.disconnect"), "power", [this, id] { model->disconnectHost(id); });
    disconnect->setEnabled(n > 0);
    QPushButton *remove = Ui::danger(T("common.remove"), "trash", [this, id] { removeHost(id); });
    remove->setEnabled(n == 0);
    if (n > 0) remove->setToolTip(T("hosts.remove.closeFirst"));
    return {disconnect, Ui::button(T("common.edit"), "edit", [this, id] { editHost(id); }),
            Ui::button(T("common.move"), "right", [this, id] { moveHost(id); }),
            Ui::button(T("hosts.send"), "share", [this, id] { sendHost(id); }), remove};
}

QList<QWidget *> Actions::terminalButtons(Session *s) {
    QList<QWidget *> out;
    const Host *h = model->host(s->hostId);
    bool local = s->hostId == Host::LOCAL;
    QString id = s->hostId;
    // The host's name first; not on a shell of this computer, where the title in the middle says enough.
    if (!local) out << hostBadge(h ? h->name : s->title);
    if (!local && h) out << terminalAction("summary", T("console.summaryOf", h->name), false, [this, id] { model->select(Position::HostItem, id); });
    // The files of this computer and of the server in place of the terminal; the same button brings the terminal back.
    if (!local && h)
        out << terminalAction(s->showFiles ? "terminal" : "folder", T(s->showFiles ? "files.back" : "files.show"), false, [this, s] {
            QTimer::singleShot(0, model, [m = model, s] {
                if (m->sessions().contains(s)) m->setShowFiles(s, !s->showFiles);
            });
        });
    Session *tab = s->tab();
    if (tab->panes().size() > 1) {
        IconButton *sync = terminalAction("sync", T(tab->synced ? "console.sync.off" : "console.sync.on"), false,
                                          [this, tab] { model->setSynced(tab, !tab->synced); });
        if (tab->synced) {
            sync->colour = Theme::sync;
            sync->hoverColour = Theme::sync;
            sync->checkedBackground = QColor(250, 204, 21, 56);
            sync->setCheckable(true);
            sync->setChecked(true);
        }
        out << sync;
    }
    if (!local && h) {
        out << terminalAction("share", T("hosts.send") + " " + h->name, false, [this, id] { sendHost(id); });
        IconButton *power = terminalAction("power", T("hosts.disconnect") + " " + h->name, false, [this, id] { model->disconnectHost(id); });
        power->setEnabled(model->connected(id) > 0);
        out << power;
    }
    // Last, on every terminal: an X that closes this pane only (the whole tab when it is the only one). After the
    // click, not during it: closing the pane throws away the bar this button sits in.
    out << terminalAction("close", T("terminal.closePane"), false, [this, s] {
        QTimer::singleShot(0, model, [m = model, s] {
            if (m->sessions().contains(s)) m->closePane(s);
        });
    });
    return out;
}

// ── Uploads ──

void Actions::upload(Session *s, const QStringList &files) {
    if (!s || s->hostId == Host::LOCAL || s->backend.isEmpty()) {
        Ui::warning(window, T("transfer.upload"), T("transfer.upload.onlyTerminal"));
        return;
    }
    // The folder the shell is in, as the terminal reports it; otherwise the home folder.
    s->view->currentFolder([this, s, files](const QString &folder) {
        auto go = [this, s, files](const QString &f) {
            QStringList chosen = files;
            if (chosen.isEmpty()) chosen = QFileDialog::getOpenFileNames(window, T("transfer.uploadTo", f));
            if (!chosen.isEmpty()) sendUpload(s, chosen, f);
        };
        if (!folder.isEmpty()) go(folder);
        else model->request("sftp.home", {{"session", s->backend}}, [go](const QJsonValue &v) { go(v.toString()); });
    });
}

void Actions::sendUpload(Session *s, const QStringList &files, const QString &folder) {
    const Host *h = model->host(s->hostId);
    QString what = files.size() == 1 ? "\"" + QFileInfo(files[0]).fileName() + "\"" + (QFileInfo(files[0]).isDir() ? " " + T("transfer.folderMark") : "")
                                     : T("files.items", int(files.size()));
    QJsonArray list;
    for (const QString &f : files) list.append(f);
    QString backendId = s->backend;
    model->request("sftp.existing", {{"session", backendId}, {"folder", folder}, {"files", list}}, [=, this](const QJsonValue &v) {
        QJsonArray exist = v.toArray();
        QStringList names;
        for (const QJsonValue &x : exist) names << x.toString();
        QString text = T("transfer.upload.ask", what, folder, h ? h->destination() : QString())
                       + (names.isEmpty() ? "" : "\n\n" + T("transfer.upload.overwrites", names.join(", ")));
        if (!Ui::confirm(window, T("transfer.upload"), text, names.isEmpty() ? T("transfer.upload") : T("transfer.overwrite"))) return;
        model->request("sftp.upload", {{"session", backendId}, {"folder", folder}, {"files", list}, {"known", exist}});
    }, [this](const QString &why) { Ui::warning(window, T("transfer.upload"), T("transfer.upload.failed", why)); });
}

void Actions::download(Session *s, const QString &name, const QString &lsCommand) {
    if (!s || s->backend.isEmpty()) return;
    QString listed = TerminalView::lsFolder(lsCommand), backendId = s->backend;
    QPointer<TerminalView> view = s->view;
    s->view->currentFolder([=, this](const QString &folder) {
        auto go = [=, this](const QString &shell) {
            QString into = QFileDialog::getExistingDirectory(window, T("transfer.downloadTo", name), QDir::homePath());
            if (into.isEmpty()) return;
            model->request("sftp.locate", {{"session", backendId}, {"shell", shell}, {"listed", listed.isEmpty() ? QJsonValue() : QJsonValue(listed)}, {"name", name}},
                           [=, this](const QJsonValue &v) {
                QJsonObject r = v.toObject();
                QString target = QDir(into).filePath(r["name"].toString());
                // A file already here: asked now. A folder: the backend asks, once it knows how many files would be replaced.
                bool replace = QFileInfo::exists(target) && !QFileInfo(target).isDir();
                if (replace && !Ui::confirm(window, T("transfer.download"), T("transfer.download.replace", target), T("transfer.overwrite"))) return;
                model->request("sftp.download", {{"session", backendId}, {"path", r["path"]}, {"into", into}, {"replace", replace}}, nullptr,
                               [this](const QString &why) { Ui::warning(window, T("transfer.download"), why); });
            }, [this](const QString &why) { Ui::warning(window, T("transfer.download"), T("transfer.download.failed", why)); });
        };
        if (!folder.isEmpty()) go(folder);
        else model->request("sftp.home", {{"session", backendId}}, [go](const QJsonValue &v) { go(v.toString()); });
    });
}

void Actions::openName(Session *s, const QString &name, const QString &lsCommand, const QPoint &at) {
    // Names with "\\" or control characters are never typed.
    if (!s || s->backend.isEmpty() || name.contains('\\')) return;
    QString listed = TerminalView::lsFolder(lsCommand), backendId = s->backend;
    QPointer<TerminalView> view = s->view;
    view->currentFolder([=, this](const QString &shell) {
        if (shell.isEmpty() || shell.contains('\\') || !view) return;
        model->request("fs.check", {{"session", backendId}, {"shell", shell}, {"listed", listed.isEmpty() ? QJsonValue() : QJsonValue(listed)}, {"name", name}},
                       [=](const QJsonValue &v) {
            if (!view) return;
            QJsonObject f = v.toObject();
            QString kind = f["kind"].toString(), typed = f["typed"].toString(), shown = "\"" + name + "\"";
            if (kind == "FOLDER") view->typeCommand("cd " + typed + " && ls");
            else if (kind == "TEXT") view->typeCommand("nano " + typed);
            else if (kind == "BINARY") view->hint(T("terminal.open.binary", shown), at);
            else if (kind == "LARGE") view->hint(T("terminal.open.large", shown), at);
            else if (kind == "OTHER") view->hint(T("terminal.open.other", shown), at);
            else if (kind == "UNTYPABLE") view->hint(T("terminal.open.untypable", shown), at);
            else view->hint(T("terminal.open.missing", shown), at);
        }, [=](const QString &why) {
            if (view) view->hint(T("terminal.open.failed", "\"" + name + "\"", why), at);
        });
    });
}
