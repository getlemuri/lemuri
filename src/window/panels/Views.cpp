#include "window/panels/Views.h"

#include "app/core/Backend.h"
#include "app/core/Model.h"
#include "app/tools/Texts.h"
#include "terminal/FilesView.h"
#include "terminal/TabStrip.h"
#include "terminal/TerminalView.h"
#include "widgets/parts/Chrome.h"
#include "widgets/look/Icons.h"
#include "widgets/look/Theme.h"
#include "widgets/parts/Ui.h"
#include "window/main/Actions.h"

#include <QApplication>
#include <QDateTime>
#include <QRegularExpression>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPainter>
#include <QPainterPath>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QSplitter>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace {
bool inside(const QString &p, const QString &folder) {
    return folder.isEmpty() || p == folder || p.startsWith(folder + "/");
}

QString folderLabel(const QString &f) {
    return f.isEmpty() ? T("tree.datacenter") : f;
}

QTableWidget *grid(const QStringList &headers) {
    auto *t = new QTableWidget(0, int(headers.size()));
    t->setHorizontalHeaderLabels(headers);
    t->verticalHeader()->hide();
    t->verticalHeader()->setDefaultSectionSize(26);
    t->setShowGrid(false);
    t->setSelectionBehavior(QAbstractItemView::SelectRows);
    t->setSelectionMode(QAbstractItemView::SingleSelection);
    t->setEditTriggers(QAbstractItemView::NoEditTriggers);
    t->setFocusPolicy(Qt::NoFocus);
    t->setMouseTracking(true);
    t->horizontalHeader()->setHighlightSections(false);
    t->setWordWrap(false);
    return t;
}

QTableWidgetItem *cell(const QString &text, const QString &key = {}) {
    auto *i = new QTableWidgetItem(text);
    i->setTextAlignment(Qt::AlignCenter);
    if (!key.isEmpty()) i->setData(Qt::UserRole, key);
    return i;
}

/** A widget centred in its table cell. */
QWidget *centred(QWidget *w) {
    auto *box = new QWidget;
    auto *l = new QHBoxLayout(box);
    l->setContentsMargins(4, 0, 4, 0);
    l->setSpacing(2);
    l->setAlignment(Qt::AlignCenter);
    l->addWidget(w);
    return box;
}

QWidget *placeholder(const QList<QWidget *> &items) {
    auto *w = new QWidget;
    auto *col = new QVBoxLayout(w);
    col->setAlignment(Qt::AlignCenter);
    col->setSpacing(10);
    for (QWidget *x : items) col->addWidget(x, 0, Qt::AlignHCenter);
    return w;
}

QLabel *bigIcon(const QString &icon, const QColor &c, int side = 40) {
    auto *l = new QLabel;
    l->setPixmap(Icons::pixmap(icon, side, c, 2, qApp->devicePixelRatio()));
    return l;
}

QLabel *strong(const QString &text, const QString &colour = Theme::text.name()) {
    auto *l = new QLabel(text);
    l->setStyleSheet(Theme::css(QString("font-size: 13px; font-weight: bold; color: %1;").arg(colour)));
    return l;
}

/** Replaces what a box holds. */
void setChildren(QWidget *box, const QList<QWidget *> &items) {
    QLayout *l = box->layout();
    while (QLayoutItem *i = l->takeAt(0)) {
        if (i->widget()) i->widget()->deleteLater();
        delete i;
    }
    for (QWidget *w : items) l->addWidget(w);
    if (auto *h = qobject_cast<QHBoxLayout *>(l)) h->addStretch(1);
}
}  // namespace

// ── Host list ──

HostListView::HostListView(Model *model, Actions *actions, Scope scope, const QString &folder, QWidget *parent)
    : QWidget(parent), model(model), actions(actions), scope(scope), folder(folder) {
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(8, 8, 8, 8);
    col->setSpacing(8);

    QString where = scope == InFolder ? folder : QString();
    QPushButton *add = Ui::primary(T("common.add"), "plus", [=] { actions->newHost(where); });
    IconButton *ssh = Ui::action("download", T("sshImport.menu"), [=] { actions->sshImport(where); });
    QPushButton *edit = Ui::button(T("common.edit"), "edit", [this] { this->actions->editHost(selected()); });
    QPushButton *console = Ui::button(T("hosts.console"), "terminal", [this] { this->model->showOrOpen(selected()); });
    QPushButton *remove = Ui::button(T("common.remove"), "trash", [this] { this->actions->removeHost(selected()); });
    QPushButton *disks = Ui::button(T("hosts.checkDisks"), "refresh", [this] { this->actions->checkDisks(shown()); });
    needSelection = {edit, console, remove};
    search = new QLineEdit;
    search->setPlaceholderText(T("hosts.search"));
    search->addAction(Icons::icon("search", 13, Theme::faint), QLineEdit::LeadingPosition);
    search->setFixedWidth(230);
    col->addWidget(Ui::bar({add, ssh, edit, console, remove, disks}, {search}));

    table = grid({T("common.status"), T("host.name"), T("host.address"), T("host.user"), T("host.folder"), T("host.system"),
                  T("hosts.col.disk"), T("hosts.col.sessions"), T("hosts.col.actions")});
    // Name, folder and system share what is left; the others keep their width.
    QList<int> widths{110, 140, 170, 90, 140, 140, 250, 80, 72};
    for (int i = 0; i < widths.size(); i++) {
        table->setColumnWidth(i, widths[i]);
        table->horizontalHeader()->setSectionResizeMode(i, i == 1 || i == 4 || i == 5 ? QHeaderView::Stretch : QHeaderView::Fixed);
    }
    table->horizontalHeader()->setMinimumSectionSize(60);
    table->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(table, &QTableWidget::customContextMenuRequested, this, [this](const QPoint &p) {
        QTableWidgetItem *i = table->itemAt(p);
        if (!i) return;
        QMenu *m = this->actions->hostMenu(table->item(i->row(), 1)->data(Qt::UserRole).toString());
        m->exec(table->viewport()->mapToGlobal(p));
        m->deleteLater();
    });
    connect(table, &QTableWidget::cellDoubleClicked, this, [this](int row, int) {
        this->model->select(Position::HostItem, table->item(row, 1)->data(Qt::UserRole).toString());
    });
    connect(table, &QTableWidget::itemSelectionChanged, this, [this] {
        for (QPushButton *b : needSelection) b->setEnabled(!selected().isEmpty());
    });
    col->addWidget(table, 1);

    connect(search, &QLineEdit::textChanged, this, &HostListView::fill);
    connect(model, &Model::hostsChanged, this, &HostListView::fill);
    connect(model, &Model::sessionChanged, this, &HostListView::fill);
    connect(model, &Model::metricsChanged, this, &HostListView::refreshDisks);
    fill();
    // Readings older than 5 minutes are read again on opening.
    qint64 now = QDateTime::currentMSecsSinceEpoch();
    for (const QString &id : shown()) {
        QJsonObject m = model->metrics().value(id);
        if (m.isEmpty() || now - qint64(m["when"].toDouble()) > 5 * 60000) model->request("metrics.refresh", {{"host", id}});
    }
}

QStringList HostListView::shown() const {
    QStringList ids;
    QString t = search->text().trimmed().toLower();
    for (const Host &h : model->hosts()) {
        bool in = scope == Received ? h.received : !h.received && (scope == All || inside(h.group, folder));
        if (in && (t.isEmpty() || h.name.toLower().contains(t) || h.address.contains(t) || h.user.toLower().contains(t))) ids << h.id;
    }
    return ids;
}

QString HostListView::selected() const {
    int r = table->currentRow();
    return r < 0 || !table->item(r, 1) || !table->selectionModel()->hasSelection() ? QString() : table->item(r, 1)->data(Qt::UserRole).toString();
}

void HostListView::fill() {
    QString keep = selected();
    QStringList ids = shown();
    table->setRowCount(int(ids.size()));
    for (int r = 0; r < ids.size(); r++) {
        const Host *h = model->host(ids[r]);
        table->setCellWidget(r, 0, centred(Ui::hostState(h->reach)));
        table->setItem(r, 0, cell(""));
        table->setItem(r, 1, cell(h->name, h->id));
        QTableWidgetItem *a = cell(h->address + ":" + QString::number(h->port));
        a->setFont(QFont("DejaVu Sans Mono", 8));
        table->setItem(r, 2, a);
        table->setItem(r, 3, cell(h->user));
        table->setItem(r, 4, cell(h->received ? T("hosts.from", h->fromContact) : folderLabel(h->group)));
        table->setItem(r, 5, cell(h->os));
        int n = model->connected(h->id);
        table->setItem(r, 7, cell(n ? QString::number(n) : QString()));
        auto *acts = new QWidget;
        auto *row = new QHBoxLayout(acts);
        row->setContentsMargins(0, 0, 0, 0);
        row->setSpacing(2);
        row->setAlignment(Qt::AlignCenter);
        QString id = h->id;
        row->addWidget(Ui::action("terminal", T("hosts.console"), [this, id] { model->showOrOpen(id); }));
        row->addWidget(Ui::action("edit", T("common.edit"), [this, id] { actions->editHost(id); }));
        table->setCellWidget(r, 8, acts);
        if (id == keep) table->selectRow(r);
    }
    refreshDisks();
    for (QPushButton *b : needSelection) b->setEnabled(!selected().isEmpty());
}

void HostListView::refreshDisks() {
    for (int r = 0; r < table->rowCount(); r++) {
        QString id = table->item(r, 1)->data(Qt::UserRole).toString();
        QJsonObject m = model->metrics().value(id);
        QJsonObject d = Ui::fullestDisk(m);
        QWidget *w;
        if (d.isEmpty()) {
            QLabel *l = Ui::faint(m.isEmpty() ? QString() : m["error"].isString() ? T("metrics.notAvailable") : T("metrics.reading"));
            if (m["error"].isString()) l->setToolTip(m["error"].toString());
            w = l;
        } else {
            QString dot = d["dot"].toString();
            QString free = Ui::measure(qint64(d["freeKb"].toDouble()));
            auto *b = new UsageBar(Ui::quota(d), dot == "/" ? T("disk.free", free) : T("disk.freeIn", free, dot), 50);
            b->setToolTip(Ui::disksTip(m));
            w = b;
        }
        table->setCellWidget(r, 6, centred(w));
    }
}

// ── Host summary ──

void CpuColumns::paintEvent(QPaintEvent *) {
    QPainter p(this);
    constexpr int SAMPLES = 30;
    double w = width() / double(SAMPLES), alt = height() - 4;
    int first = SAMPLES - int(values.size());
    for (int i = 0; i < values.size(); i++) {
        double h = std::max(1.0, values[i] * alt);
        p.fillRect(QRectF((first + i) * w + w * 0.12, height() - h, std::max(1.0, w * 0.76), h), Theme::accent);
    }
}

namespace {
/** A details row: faint caption, then the value; a thin line below. */
void detail(QGridLayout *g, int row, const QString &caption, QWidget *value) {
    auto *c = Ui::faint(caption);
    c->setMinimumWidth(140);
    auto *line = new QFrame;
    line->setFixedHeight(1);
    line->setStyleSheet(Theme::css("background: @border;"));
    g->addWidget(c, row * 2, 0);
    g->addWidget(value ? value : Ui::faint(T("common.notSet")), row * 2, 1);
    g->addWidget(line, row * 2 + 1, 0, 1, 2);
}

QWidget *hbox() {
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(6);
    return w;
}

QString duration(qint64 s) {
    qint64 d = s / 86400, h = s % 86400 / 3600, m = s % 3600 / 60;
    return d > 0 ? (d == 1 ? T("uptime.day", d, h) : T("uptime.days", d, h)) : h > 0 ? T("uptime.hours", h, m) : T("uptime.minutes", m);
}

QString tone(std::optional<double> score) {
    return !score ? "none" : *score < 0.5 ? "bad" : *score < 0.8 ? "warn" : "ok";
}

QColor toneColour(const QString &t) {
    return t == "bad" ? Theme::critical : t == "warn" ? Theme::amber : t == "ok" ? Theme::ok : Theme::border;
}

std::optional<double> scoreOf(const QList<QJsonObject> &checks) {
    double got = 0, all = 0;
    for (const QJsonObject &c : checks) {
        double w = c["weight"].toDouble();
        all += w;
        QString l = c["level"].toString();
        got += w * (l == "OK" ? 1 : l == "WARN" ? 0.5 : 0);
    }
    if (all == 0) return std::nullopt;
    return got / all;
}

/** The small ring of a security card, with its percentage. */
class Ring : public QWidget {
public:
    explicit Ring(double s) : score(s) { setFixedSize(42, 42); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QRectF r(4, 4, 34, 34);
        p.setPen(QPen(Theme::track, 4));
        p.drawEllipse(r);
        p.setPen(QPen(toneColour(tone(score)), 4, Qt::SolidLine, Qt::RoundCap));
        p.drawArc(r, 90 * 16, int(-360 * 16 * std::clamp(score, 0.0, 1.0)));
        QFont f = font();
        f.setPixelSize(10);
        f.setBold(true);
        p.setFont(f);
        p.setPen(Theme::text);
        p.drawText(rect(), Qt::AlignCenter, QString("%1%").arg(qRound(score * 100)));
    }

private:
    double score;
};

/** The switch of a check: on and green when safe, on and amber when it could be better, off and red when not. */
class Switch : public QWidget {
public:
    explicit Switch(const QString &l) : level(l) { setFixedSize(26, 14); }

protected:
    void paintEvent(QPaintEvent *) override {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QColor c = level == "OK" ? Theme::ok : level == "WARN" ? Theme::amber : Theme::critical;
        p.setPen(Qt::NoPen);
        p.setBrush(c);
        p.drawRoundedRect(rect(), 7, 7);
        p.setBrush(Qt::white);
        double x = level == "BAD" ? 7 : width() - 7;
        p.drawEllipse(QPointF(x, height() / 2.0), 5, 5);
    }

private:
    QString level;
};

QString shown(const QJsonObject &c) {
    QString v = c["value"].toString();
    QString id = c["id"].toString();
    if (v.isEmpty()) return {};
    if (id == "server.updates") return T("summary.updates.security", v);
    if (id == "conn.version") return v.split(QRegularExpression("\\s+")).first().replace('_', ' ');
    static const QRegularExpression setting("^[A-Z][A-Za-z]+ \\S+$");
    if (id.startsWith("server.") && setting.match(v).hasMatch()) return v.mid(v.indexOf(' ') + 1);
    return QString(v).replace("@openssh.com", "");
}
}  // namespace

HostSummaryView::HostSummaryView(Model *model, Actions *actions, const QString &hostId, QWidget *parent)
    : QWidget(parent), model(model), actions(actions), id(hostId) {
    const Host *h = model->host(id);
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

    updatesBox = hbox();
    rebootBox = hbox();
    upFor = new QLabel;
    auto *details = new QWidget;
    grid = new QGridLayout(details);
    grid->setContentsMargins(0, 0, 0, 0);
    grid->setVerticalSpacing(3);
    grid->setColumnStretch(1, 1);
    showDetails();

    cpu = new Gauge("CPU");
    ram = new Gauge(T("summary.memory"));
    disk = new Gauge(T("summary.diskRoot"));
    security = new Gauge(T("security.gauge"), true);
    auto *rings = new QWidget;
    auto *rl = new QHBoxLayout(rings);
    rl->setContentsMargins(0, 0, 0, 0);
    for (Gauge *x : {cpu, ram, disk, security}) rl->addWidget(x, 1);
    note = Ui::faint("");
    note->setWordWrap(true);
    auto *resources = new QWidget;
    auto *resCol = new QVBoxLayout(resources);
    resCol->setContentsMargins(0, 0, 0, 0);
    resCol->addWidget(rings);
    resCol->addWidget(note);
    resCol->addStretch(1);
    auto *line = new QHBoxLayout;
    line->setSpacing(8);
    line->addWidget(Ui::panel(T("common.status"), details), 1);
    line->addWidget(Ui::panel(T("summary.resources"), resources), 1);
    col->addLayout(line);

    securityBody = new QWidget;
    new QVBoxLayout(securityBody);
    securityBody->layout()->setContentsMargins(0, 0, 0, 0);
    IconButton *again = Ui::action("refresh", T("security.refresh"), [this] {
        haveSecurity = false;
        showSecurity();
        this->model->request(this, "host.security", {{"host", id}, {"force", true}}, [this](const QJsonValue &v) {
            securityReport = v.toObject();
            haveSecurity = true;
            showSecurity();
        }, [](const QString &) {});
    });
    col->addWidget(Ui::panel(T("security.title"), securityBody, again));

    auto *disksBox = new QWidget;
    disks = new QVBoxLayout(disksBox);
    disks->setContentsMargins(0, 0, 0, 0);
    col->addWidget(Ui::panel(T("summary.disks"), disksBox));
    columns = new CpuColumns;
    columns->setFixedHeight(120);
    col->addWidget(Ui::panel(T("summary.cpuHistory", 30 * 4 / 60), columns));

    // Notes: saved with the host.
    auto *notesBox = new QWidget;
    auto *nc = new QVBoxLayout(notesBox);
    nc->setContentsMargins(0, 0, 0, 0);
    noteText = new QPlainTextEdit(h->note);
    noteText->setPlaceholderText(T("notes.prompt"));
    noteText->setStyleSheet(Theme::css("font-family: 'DejaVu Sans Mono';"));
    noteText->setMinimumHeight(100);
    noteUnsaved = new QLabel(T("notes.unsaved"));
    noteUnsaved->setStyleSheet(Theme::css("color: @warn;"));
    savedNote = h->note;
    noteSave = Ui::primary(T("common.save"), "save", [this] {
        const Host *x = this->model->host(id);
        if (!x) return;
        QJsonObject c = x->json;
        c["note"] = noteText->toPlainText();
        this->model->request("host.save", {{"host", c}});
    });
    connect(noteText, &QPlainTextEdit::textChanged, this, &HostSummaryView::refreshNote);
    refreshNote();
    nc->addWidget(Ui::bar({noteSave}, {noteUnsaved}));
    nc->addWidget(noteText);
    col->addWidget(Ui::panel(T("summary.notes"), notesBox));
    col->addStretch(1);

    connect(model, &Model::metricsChanged, this, [this](const QString &changed) {
        if (changed != id) return;
        show();
        this->model->request(this, "metrics.history", {{"host", id}}, [this](const QJsonValue &v) {
            columns->values.clear();
            for (const QJsonValue &x : v.toObject()["cpu"].toArray()) columns->values << x.toDouble();
            columns->update();
        });
    });
    round = new QTimer(this);
    round->setInterval(4000);
    connect(round, &QTimer::timeout, this, [this] {
        this->model->request("metrics.refresh", {{"host", id}});
        // The backend keeps the updates for minutes: asking once a minute costs nothing in between.
        if (++ticks % 15 == 0) askUpdates(false);
    });
    showUpdates({});
    showSecurity();
    if (h->reach != Host::Reach::Offline) {
        // Without forcing: a host that refuses access must be left alone.
        model->request("metrics.refresh", {{"host", id}});
        askUpdates(false);
        round->start();
    }
    show();
    model->request(this, "metrics.history", {{"host", id}}, [this](const QJsonValue &v) {
        for (const QJsonValue &x : v.toObject()["cpu"].toArray()) columns->values << x.toDouble();
        columns->update();
    });
}

/** The table of the host's data: drawn again in place when the host changes, the readings around it stay. */
void HostSummaryView::askUpdates(bool force) {
    model->request(this, "host.updates", {{"host", id}, {"force", force}}, [this](const QJsonValue &v) {
        updates = v.toObject();
        haveUpdates = true;
        showUpdates(updates);
        showSecurity();
    }, [](const QString &) {});
    model->request(this, "host.security", {{"host", id}, {"force", force}}, [this](const QJsonValue &v) {
        securityReport = v.toObject();
        haveSecurity = true;
        showSecurity();
    }, [](const QString &) {});
}

void HostSummaryView::showDetails() {
    const Host *h = model->host(id);
    if (!h) return;
    // The boxes filled by the readings are kept: only taken out of the grid, to go back in their rows.
    for (QWidget *keep : {updatesBox, rebootBox, static_cast<QWidget *>(upFor)}) grid->removeWidget(keep);
    while (QLayoutItem *i = grid->takeAt(0)) {
        if (i->widget()) i->widget()->deleteLater();
        delete i;
    }
    QString auth = h->auth == Host::Auth::Key ? (h->key.isEmpty() ? T("summary.auth.keyAuto") : T("summary.auth.key", h->key))
                 : h->auth == Host::Auth::Agent ? T("auth.agent") : T("summary.auth.password");
    int r = 0;
    auto *state = hbox();
    setChildren(state, {Ui::hostState(h->reach)});
    detail(grid, r++, T("common.status"), state);
    detail(grid, r++, T("summary.updates"), updatesBox);
    detail(grid, r++, T("summary.reboot"), rebootBox);
    detail(grid, r++, T("host.address"), Ui::code(h->address + ":" + QString::number(h->port)));
    detail(grid, r++, T("host.user"), new QLabel(h->user));
    detail(grid, r++, T("summary.jump"), h->jump.isEmpty() ? nullptr : Ui::code(h->jump));
    detail(grid, r++, T("host.auth"), new QLabel(auth));
    detail(grid, r++, T("host.os"), h->os.isEmpty() ? nullptr : new QLabel(h->os));
    detail(grid, r++, T("summary.upFor"), upFor);
    detail(grid, r++, T("host.folder"), new QLabel(folderLabel(h->group)));
    detail(grid, r++, T("summary.connected"), new QLabel(QString::number(model->connected(id))));
}

void HostSummaryView::refreshNote() {
    bool changed = noteText->toPlainText() != savedNote;
    noteSave->setEnabled(changed);
    noteUnsaved->setVisible(changed);
}

void HostSummaryView::refresh() {
    const Host *h = model->host(id);
    if (!h) return;
    showDetails();
    // The note: what was saved is the new reference; a note changed elsewhere replaces the text only if untouched here.
    if (noteText->toPlainText() == savedNote && h->note != savedNote) noteText->setPlainText(h->note);
    savedNote = h->note;
    refreshNote();
    show();
    showUpdates(updates);
    showSecurity();
    // Reachable again: the readings start, as when the page opened.
    if (h->reach != Host::Reach::Offline && !round->isActive()) {
        model->request("metrics.refresh", {{"host", id}});
        askUpdates(false);
        round->start();
    }
}

void HostSummaryView::showUpdates(const QJsonObject &u) {
    const Host *h = model->host(id);
    if (!h) return;
    if (!haveUpdates) {
        QString text = h->reach == Host::Reach::Offline ? T("summary.unreachable") : T("summary.updates.checking");
        setChildren(updatesBox, {Ui::chip("neutral", text)});
        setChildren(rebootBox, {Ui::chip("neutral", text)});
        return;
    }
    if (u["error"].isString()) {
        setChildren(updatesBox, {Ui::faint(u["error"].toString())});
        setChildren(rebootBox, {Ui::faint(u["error"].toString())});
        return;
    }
    if (u["packages"].isNull()) setChildren(updatesBox, {Ui::chip("neutral", T("summary.updates.unknown"))});
    else if (u["packages"].toInt() == 0) setChildren(updatesBox, {Ui::chip("ok", T("summary.updates.none"))});
    else {
        QList<QWidget *> l{Ui::chip("warn", T("summary.updates.count", u["packages"].toInt()))};
        if (u["security"].toInt() > 0) l << Ui::chip("error", T("summary.updates.security", u["security"].toInt()));
        setChildren(updatesBox, l);
    }
    if (u["reboot"].isNull()) setChildren(rebootBox, {Ui::chip("neutral", T("summary.reboot.unknown"))});
    else if (!u["reboot"].toBool()) setChildren(rebootBox, {Ui::chip("ok", T("summary.reboot.no"))});
    else {
        QList<QWidget *> l{Ui::chip("error", T("summary.reboot.needed"))};
        if (u["rebootBecause"].isString()) {
            QLabel *why = Ui::faint(u["rebootBecause"].toString());
            why->setToolTip(u["rebootBecause"].toString());
            l << why;
        }
        setChildren(rebootBox, l);
    }
}

void HostSummaryView::show() {
    const Host *h = model->host(id);
    if (!h) return;
    QJsonObject e = model->metrics().value(id);
    QJsonObject r = e["resources"].toObject();
    while (QLayoutItem *i = disks->takeAt(0)) {
        if (i->widget()) i->widget()->deleteLater();
        delete i;
    }
    if (r.isEmpty()) {
        QString why = h->reach == Host::Reach::Offline ? T("summary.unreachable") : e["error"].isString() ? e["error"].toString() : T("metrics.reading");
        for (Gauge *g : {cpu, ram, disk}) g->set(std::nullopt, "");
        note->setText(why);
        upFor->clear();
        disks->addWidget(Ui::faint(why));
        return;
    }
    cpu->set(r["cpuUsage"].isNull() ? std::nullopt : std::optional<double>(r["cpuUsage"].toDouble()), QString("%1 vCPU").arg(r["cpu"].toInt()));
    qint64 total = qint64(r["memTotalKb"].toDouble()), used = total - qint64(r["memFreeKb"].toDouble());
    ram->set(total > 0 ? std::optional<double>(double(used) / total) : std::nullopt, T("summary.usedOf", Ui::measure(used), Ui::measure(total)));
    QJsonObject root;
    for (const QJsonValue &v : r["disks"].toArray())
        if (v.toObject()["dot"].toString() == "/") root = v.toObject();
    disk->set(root.isEmpty() ? std::nullopt : std::optional<double>(Ui::quota(root)),
              root.isEmpty() ? QString() : T("summary.usedOf", Ui::measure(qint64(root["usedKb"].toDouble())), Ui::measure(qint64(root["totalKb"].toDouble()))));
    upFor->setText(duration(qint64(r["onForS"].toDouble())));
    note->setText(e["error"].isString() ? T("metrics.stale", e["error"].toString()) : QString());
    for (const QJsonValue &v : r["disks"].toArray()) {
        QJsonObject d = v.toObject();
        auto *row = new QWidget;
        auto *l = new QHBoxLayout(row);
        l->setContentsMargins(0, 2, 0, 2);
        l->setSpacing(12);
        QLabel *dot = Ui::code(d["dot"].toString());
        dot->setFixedWidth(120);
        l->addWidget(dot);
        l->addWidget(new UsageBar(Ui::quota(d), T("summary.diskLine", Ui::measure(qint64(d["usedKb"].toDouble())), Ui::measure(qint64(d["totalKb"].toDouble())),
                                                  Ui::measure(qint64(d["freeKb"].toDouble()))), 260));
        l->addStretch(1);
        disks->addWidget(row);
    }
}

void HostSummaryView::showSecurity() {
    const Host *h = model->host(id);
    if (!h) return;
    QList<QJsonObject> checks;
    for (const QJsonValue &v : securityReport["checks"].toArray()) checks << v.toObject();
    // The security updates count of the summary, as a check of the server.
    if (haveUpdates && !updates["error"].isString() && !updates["security"].isNull()) {
        checks.erase(std::remove_if(checks.begin(), checks.end(), [](const QJsonObject &c) { return c["id"].toString() == "server.updates"; }), checks.end());
        int n = updates["security"].toInt();
        checks << QJsonObject{{"id", "server.updates"}, {"level", n > 0 ? "BAD" : "OK"}, {"value", QString::number(n)}, {"fix", QJsonValue()}, {"weight", 2}};
    }
    QWidget *box = securityBody;
    QLayout *l = box->layout();
    while (QLayoutItem *i = l->takeAt(0)) {
        if (i->widget()) i->widget()->deleteLater();
        delete i;
    }
    QString waiting = h->reach == Host::Reach::Offline ? T("summary.unreachable") : T("summary.updates.checking");
    if (!haveSecurity || checks.isEmpty()) {
        l->addWidget(Ui::faint(haveSecurity && securityReport["error"].isString() ? securityReport["error"].toString() : waiting));
        security->set(std::nullopt, "");
        return;
    }
    auto score = scoreOf(checks);
    int bad = int(std::count_if(checks.begin(), checks.end(), [](const QJsonObject &c) { return c["level"].toString() != "OK"; }));
    security->set(score, bad == 0 ? T("security.allGood") : T("security.toLook", bad));
    auto *cards = new QWidget;
    auto *row = new QHBoxLayout(cards);
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(10);
    struct Area { QString prefix, icon, title; };
    for (const Area &a : {Area{"conn.", "lock", "security.area.connection"}, Area{"client.", "key", "security.area.key"},
                          Area{"server.", "server", "security.area.server"}}) {
        QList<QJsonObject> mine;
        for (const QJsonObject &c : checks)
            if (c["id"].toString().startsWith(a.prefix)) mine << c;
        // Red, then amber, then green; inside each, what counts more first.
        std::sort(mine.begin(), mine.end(), [](const QJsonObject &x, const QJsonObject &y) {
            auto rank = [](const QString &level) { return level == "BAD" ? 0 : level == "WARN" ? 1 : 2; };
            int a1 = rank(x["level"].toString()), b1 = rank(y["level"].toString());
            return a1 != b1 ? a1 < b1 : x["weight"].toInt() > y["weight"].toInt();
        });
        row->addWidget(securityCard(a.prefix, a.icon, T(a.title.toLatin1().constData()), mine), 1);
    }
    l->addWidget(cards);
    QString text = T("security.readOnly");
    if (std::any_of(checks.begin(), checks.end(), [](const QJsonObject &c) {
            return c["level"].toString() != "OK" && c["fix"].isString() && c["id"].toString().startsWith("server.");
        }))
        text += " " + T("security.fixHint");
    if (bad > 0) text += " " + T("security.hover");
    QLabel *n = Ui::faint(text);
    n->setWordWrap(true);
    n->setStyleSheet(Theme::css("color: @faint; font-size: 11px;"));
    l->addWidget(n);
}

QWidget *HostSummaryView::securityCard(const QString &prefix, const QString &icon, const QString &title, const QList<QJsonObject> &checks) {
    auto score = scoreOf(checks);
    QString t = tone(score);
    QColor c = toneColour(t);
    auto *card = new QFrame;
    card->setObjectName("card");
    card->setStyleSheet(Theme::css(QString("#card { background: @veil; border: 1px solid @border; border-top: 3px solid %1; border-radius: 6px; }").arg(c.name())));
    auto *col = new QVBoxLayout(card);
    col->setContentsMargins(12, 10, 12, 12);
    col->setSpacing(10);
    auto *head = new QHBoxLayout;
    head->setSpacing(10);
    auto *badge = new QLabel;
    badge->setFixedSize(34, 34);
    badge->setAlignment(Qt::AlignCenter);
    QColor bg = t == "none" ? Theme::on : QColor(int(c.red() * 0.35), int(c.green() * 0.35), int(c.blue() * 0.35));
    badge->setStyleSheet(Theme::css(QString("background: %1; border-radius: 17px;").arg(bg.name())));
    badge->setPixmap(Icons::pixmap(icon, 18, t == "none" ? Theme::text : c.lighter(140), 2, devicePixelRatioF()));
    head->addWidget(badge);
    auto *names = new QVBoxLayout;
    names->setSpacing(1);
    auto *tl = new QLabel(title);
    tl->setStyleSheet(Theme::css("font-size: 14px; font-weight: bold;"));
    names->addWidget(tl);
    int toLook = int(std::count_if(checks.begin(), checks.end(), [](const QJsonObject &x) { return x["level"].toString() != "OK"; }));
    names->addWidget(Ui::faint(checks.isEmpty() ? QString() : toLook == 0 ? T("security.allGood") : T("security.toLook", toLook)));
    head->addLayout(names);
    head->addStretch(1);
    if (score) head->addWidget(new Ring(*score));
    col->addLayout(head);
    if (checks.isEmpty()) {
        QLabel *e = Ui::faint(prefix == "server." ? (securityReport["error"].isString() ? securityReport["error"].toString() : T("security.serverUnknown"))
                                                  : T("security.keyUnknown"));
        e->setWordWrap(true);
        col->addWidget(e);
    }
    for (const QJsonObject &x : checks) {
        QString cid = x["id"].toString();
        auto *rowBox = new QWidget;
        auto *rc = new QVBoxLayout(rowBox);
        rc->setContentsMargins(0, 0, 0, 0);
        rc->setSpacing(3);
        auto *line = new QHBoxLayout;
        line->setSpacing(8);
        line->addWidget(new Switch(x["level"].toString()));
        line->addWidget(new QLabel(T(("security." + cid).toLatin1().constData())));
        line->addStretch(1);
        QString s = shown(x);
        if (!s.isEmpty()) {
            QLabel *v = Ui::code(s);
            v->setStyleSheet(Theme::css("font-family: 'DejaVu Sans Mono'; font-size: 11px; color: @faint;"));
            line->addWidget(v);
        }
        rc->addLayout(line);
        QString found = cid == "server.updates" ? s : x["value"].toString();
        QString name = T(("security." + cid).toLatin1().constData());
        rowBox->setToolTip(x["level"].toString() == "OK" ? name + (found.isEmpty() ? "" : ": " + found)
                                                         : T(("security." + cid + ".advice").toLatin1().constData()) + (found.isEmpty() ? "" : "\n\n" + found));
        if (x["level"].toString() != "OK" && x["fix"].isString()) {
            bool isBad = x["level"].toString() == "BAD";
            auto *fix = new QLabel((cid.startsWith("client.") ? "$ " : "→ ") + x["fix"].toString());
            fix->setStyleSheet(Theme::css(QString("font-family: 'DejaVu Sans Mono'; font-size: 11px; border-radius: 3px; padding: 1px 6px; background: %1; color: %2;")
                                       .arg(isBad ? Theme::errorBack.name() : Theme::warnBack.name(), isBad ? Theme::errorText.name() : Theme::warn.name())));
            fix->setTextInteractionFlags(Qt::TextSelectableByMouse);
            auto *in = new QHBoxLayout;
            in->setContentsMargins(34, 0, 0, 0);
            in->addWidget(fix);
            in->addStretch(1);
            rc->addLayout(in);
        }
        col->addWidget(rowBox);
    }
    col->addStretch(1);
    return card;
}

// ── Console ──

namespace {
/** Over a terminal while it connects: a turning arc, the host and where. */
class Connecting : public QWidget {
public:
    Connecting(Model *model, Session *s, QWidget *parent) : QWidget(parent), model(model), s(s) {
        setAttribute(Qt::WA_StyledBackground);
        setStyleSheet(Theme::css("background: @terminalBg;"));
        auto *col = new QVBoxLayout(this);
        col->setAlignment(Qt::AlignCenter);
        col->setSpacing(10);
        spinner = new QWidget;
        spinner->setFixedSize(34, 34);
        spinner->installEventFilter(this);
        col->addWidget(spinner, 0, Qt::AlignHCenter);
        const Host *h = model->host(s->hostId);
        what = new QLabel;
        what->setStyleSheet(Theme::css("color: @actionHover; font-size: 14px;"));
        what->setAlignment(Qt::AlignCenter);
        col->addWidget(what, 0, Qt::AlignHCenter);
        why = new QLabel;
        why->setStyleSheet(Theme::css("color: @errorSoft; font-size: 13px;"));
        why->setWordWrap(true);
        why->setMaximumWidth(520);
        why->setAlignment(Qt::AlignCenter);
        col->addWidget(why, 0, Qt::AlignHCenter);
        auto *where = new QLabel(h ? h->destination() : QString());
        where->setStyleSheet(Theme::css("color: @actionIcon; font-size: 12px; font-family: 'DejaVu Sans Mono';"));
        col->addWidget(where, 0, Qt::AlignHCenter);
        buttons = new QWidget;
        auto *row = new QHBoxLayout(buttons);
        row->setSpacing(8);
        row->addWidget(Ui::primary(T("session.retry"), "refresh", [model, s] { model->retry(s); }));
        row->addWidget(Ui::button(T("session.showTerminal"), "terminal", [this] {
            uncovered = true;
            refresh();
        }));
        row->addWidget(Ui::button(T("window.close"), "close", [model, s] { model->closePane(s); }));
        col->addWidget(buttons, 0, Qt::AlignHCenter);
        timer.setInterval(30);
        QObject::connect(&timer, &QTimer::timeout, spinner, [this] {
            angle = (angle + 8) % 360;
            spinner->update();
        });
        QObject::connect(model, &Model::sessionChanged, this, [this](Session *x) {
            if (x == this->s) refresh();
        });
        refresh();
    }

    void refresh() {
        bool connecting = s->state == Session::State::Connecting, failed = s->state == Session::State::Error && !uncovered;
        setVisible(connecting || failed);
        const Host *h = model->host(s->hostId);
        QString name = h ? h->name : QString();
        what->setText(connecting ? T("session.connectingTo", name) : T("session.failedTo", name));
        QString e = s->error;
        if (!e.isEmpty()) e[0] = e[0].toUpper();
        why->setText(e);
        why->setVisible(failed && !e.isEmpty());
        buttons->setVisible(failed);
        if (connecting) timer.start();
        else timer.stop();
        spinner->update();
        raise();
    }

protected:
    bool eventFilter(QObject *o, QEvent *e) override {
        if (o == spinner && e->type() == QEvent::Paint) {
            QPainter p(spinner);
            p.setRenderHint(QPainter::Antialiasing);
            QRectF r(3, 3, 28, 28);
            if (s->state == Session::State::Connecting) {
                p.setPen(QPen(QColor(124, 196, 255, 60), 3));
                p.drawEllipse(r);
                p.setPen(QPen(Theme::ghostAccent, 3, Qt::SolidLine, Qt::RoundCap));
                p.drawArc(r, -angle * 16, 100 * 16);
            } else {
                Icons::paint(p, "plug-off", QRectF(2, 2, 30, 30), Theme::failure);
            }
            return true;
        }
        return false;
    }

private:
    Model *model;
    Session *s;
    QWidget *spinner, *buttons;
    QLabel *what, *why;
    QTimer timer;
    int angle = 0;
    bool uncovered = false;
};

/**
 * A slot: a band across its top (as Terminator's title bars: the host, what the shell says it is doing, the
 * commands on the right), the terminal right under it, and the cover over the terminal; an amber edge when synced.
 */
class Slot : public QWidget {
public:
    Slot(QWidget *view, QWidget *bar, Connecting *cover, bool synced, bool ghost) : view(view), bar(bar), cover(cover) {
        setAttribute(Qt::WA_StyledBackground);
        setObjectName("slot");
        QString bg = ghost ? "transparent" : Theme::terminalBg.name();
        setStyleSheet(Theme::css(synced ? "#slot { background: " + bg + "; border: 2px solid @syncedBorder; }" : "#slot { background: " + bg + "; }"));
        int pad = synced ? 4 : 0;
        auto *l = new QVBoxLayout(this);
        l->setContentsMargins(pad, pad, pad, pad);
        l->setSpacing(0);
        l->addWidget(bar);
        l->addWidget(view, 1);
        // The files view keeps its own sheet.
        if (qobject_cast<TerminalView *>(view)) view->setStyleSheet(Theme::css("border: none;"));
        cover->setParent(this);
    }

protected:
    void resizeEvent(QResizeEvent *) override {
        // Over the terminal only: the band stays usable while it connects.
        QRect in = rect().marginsRemoved(layout()->contentsMargins());
        in.setTop(in.top() + bar->sizeHint().height());
        cover->setGeometry(in);
        cover->raise();
        if (auto *t = qobject_cast<TerminalView *>(view)) t->setScrollTop(0);
    }

private:
    QWidget *view, *bar;
    Connecting *cover;
};
}  // namespace

ConsoleView::ConsoleView(Model *model, Actions *actions, const QString &hostId, QWidget *parent)
    : QWidget(parent), model(model), actions(actions), hostId(hostId) {
    build(nullptr);
}

ConsoleView::ConsoleView(Model *model, Actions *actions, QWidget *extra, QWidget *parent)
    : QWidget(parent), model(model), actions(actions), ghost(true) {
    build(extra);
}

void ConsoleView::build(QWidget *extra) {
    setAttribute(Qt::WA_StyledBackground, !ghost);
    setStyleSheet(Theme::css(ghost ? "" : "ConsoleView { background: transparent; }"));
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    strip = new TabStrip;
    strip->ghost = ghost;
    if (extra) {
        auto *top = new QHBoxLayout;
        top->setContentsMargins(0, 0, 6, 0);
        top->setSpacing(4);
        top->addWidget(strip, 1);
        top->addWidget(extra);
        col->addLayout(top);
    } else {
        col->addWidget(strip);
    }
    area = new QWidget;
    areaLayout = new QVBoxLayout(area);
    areaLayout->setContentsMargins(0, 0, 0, 0);
    col->addWidget(area, 1);
    connect(strip, &TabStrip::currentChanged, this, [this](int i) {
        if (i < 0 || i >= shownTabs.size()) return;
        if (ghost) shown = shownTabs[i];
        this->model->showSession(shownTabs[i]);
    });
    connect(strip, &TabStrip::tabClicked, this, &ConsoleView::terminalsWanted);
    connect(strip, &TabStrip::plusClicked, this, &ConsoleView::terminalsWanted);
    connect(strip, &TabStrip::closeRequested, this, [this](int i) {
        if (i >= 0 && i < shownTabs.size()) this->model->closeSession(shownTabs[i]);
    });
    connect(strip, &TabStrip::plusClicked, this, [this] {
        Session *t = ghost ? shown : nullptr;
        this->model->openSession(t ? t->hostId : this->hostId);
    });
    connect(model, &Model::sessionsChanged, this, &ConsoleView::rebuild);
    connect(model, &Model::sessionChanged, this, &ConsoleView::refreshStrip);
    if (ghost) connect(model, &Model::positionChanged, this, &ConsoleView::rebuild);
    rebuild();
}

void ConsoleView::setPage(QWidget *p) {
    if (page && page != p) page->hide();
    page = p;
    if (p && layout()->indexOf(p) < 0) static_cast<QVBoxLayout *>(layout())->addWidget(p, 1);
    if (p) p->show();
    area->setVisible(!p);
}

/** Ghost mode: the active tab of the host chosen in the tree, otherwise the one shown, otherwise the last one opened. */
Session *ConsoleView::chooseTab() {
    Session *next = nullptr;
    if (model->position().kind == Position::HostItem) next = model->active().value(model->position().key);
    if (next && !model->sessions().contains(next)) next = nullptr;
    if (!next && shown && model->sessions().contains(shown)) next = shown;
    if (!next && !model->sessions().isEmpty()) next = model->sessions().last()->tab();
    return next ? next->tab() : nullptr;
}

ConsoleView::~ConsoleView() {
    release();
}

/** The terminals belong to their sessions: taken out before the containers they sit in are thrown away. */
void ConsoleView::release() {
    for (Session *s : model->sessions())
    {
        if (s->view && s->view->parentWidget() && this->isAncestorOf(s->view)) {
            s->view->hide();
            s->view->setGhost(false);
            s->view->setParent(nullptr);
        }
        if (s->files && s->files->parentWidget() && this->isAncestorOf(s->files)) {
            s->files->hide();
            s->files->setParent(nullptr);
        }
    }
}

void ConsoleView::refreshStrip() {
    shownTabs = model->tabs();
    Session *active = ghost ? shown : model->active().value(hostId);
    QList<TabStrip::Item> items;
    int current = -1;
    for (int i = 0; i < shownTabs.size(); i++) {
        Session *s = shownTabs[i];
        const Host *h = model->host(s->hostId);
        using State = Session::State;
        TabStrip::State st = s->state == State::Connected ? TabStrip::State::Connected : s->state == State::Error ? TabStrip::State::Error
                           : s->state == State::Closed ? TabStrip::State::Closed : TabStrip::State::Connecting;
        QString stateText = Ui::sessionStateText(s->state);
        items.append({s->title, s->title + "\n" + (h ? h->destination() : QString()) + " · " + stateText, st});
        if (s == active) current = i;
    }
    strip->setTabs(items, current);
}

void ConsoleView::rebuild() {
    // The part that has the keyboard gets it back.
    QWidget *focus = QApplication::focusWidget();
    release();
    while (QLayoutItem *i = areaLayout->takeAt(0)) {
        if (i->widget()) i->widget()->deleteLater();
        delete i;
    }
    if (ghost) shown = chooseTab();
    refreshStrip();
    Session *active = ghost ? shown : model->active().value(hostId);
    const Host *h = model->host(hostId);
    QWidget *body;
    if (ghost && !active) {
        body = new QWidget;
    } else if (model->sessions().isEmpty()) {
        auto *dest = new QWidget;
        auto *dl = new QHBoxLayout(dest);
        dl->setContentsMargins(0, 0, 0, 0);
        dl->addWidget(Ui::faint(T("sessions.col.destination")));
        dl->addWidget(Ui::code(h ? h->destination() : QString()));
        body = placeholder({bigIcon("terminal", Theme::faint), strong(T("sessions.none")), dest,
                            Ui::primary(T("console.open"), "terminal", [this] { model->openSession(hostId); })});
        strip->hide();
    } else if (!active || !model->sessions().contains(active)) {
        body = placeholder({bigIcon("terminal", Theme::faint), strong(T("console.noSessionsOn", h ? h->name : QString())),
                            Ui::primary(T("console.open"), "terminal", [this] { model->openSession(hostId); })});
        strip->show();
    } else {
        Session *tab = active->tab();
        body = tab->layout() ? draw(tab->layout(), tab) : slot(tab, false);
        strip->show();
        for (Session *p : tab->panes()) p->view->setGhost(ghost);
        Session *keys = nullptr;
        for (Session *p : tab->panes())
            if (p->view == focus) keys = p;
        if (!keys) keys = tab;
        QTimer::singleShot(0, keys->view, [v = QPointer<TerminalView>(keys->view)] {
            if (v) v->setFocus();
        });
    }
    areaLayout->addWidget(body);
}

QWidget *ConsoleView::draw(Pane *p, Session *tab) {
    bool synced = tab->synced && tab->panes().size() > 1;
    if (!p->divided()) return slot(p->session(), synced);
    auto *split = new QSplitter(p->sideBySide() ? Qt::Horizontal : Qt::Vertical);
    split->setHandleWidth(2);
    split->setChildrenCollapsible(false);
    split->setStyleSheet(Theme::css(QString(ghost ? "QSplitter { background: transparent; }" : "QSplitter { background: @terminalBg; }") + " QSplitter::handle { background: @border; }"
                         "QSplitter::handle:hover { background: @checkEdge; }"));
    split->addWidget(draw(p->first(), tab));
    split->addWidget(draw(p->second(), tab));
    // Where the separator was, once the splitter knows its size.
    QTimer::singleShot(0, split, [split, p] {
        int total = p->sideBySide() ? split->width() : split->height();
        int a = int(total * p->divider);
        split->setSizes({a, total - a});
    });
    connect(split, &QSplitter::splitterMoved, split, [split, p] {
        QList<int> s = split->sizes();
        if (s.size() == 2 && s[0] + s[1] > 0) p->divider = double(s[0]) / (s[0] + s[1]);
    });
    return split;
}

QWidget *ConsoleView::slot(Session *s, bool synced) {
    auto *bar = new QWidget;
    bar->setObjectName("terminalActions");
    bar->setAttribute(Qt::WA_StyledBackground);
    // No edge at its foot: it fades into the terminal's background (in ghost mode, into the see-through).
    bar->setStyleSheet(Theme::css(ghost ? "#terminalActions { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 rgba(255,255,255,0.08), stop:1 rgba(255,255,255,0)); }"
                                        : "#terminalActions { background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 @terminalActions, stop:0.45 @terminalActions, stop:1 @terminalBg); }"));
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(6, 2, 6, 2);
    row->setSpacing(2);
    // The host on the left, the commands on the right and, in the middle of the band, what the shell calls itself
    // (user@host: folder): the two sides stretch alike, so the title stays at the centre of the terminal.
    QList<QWidget *> buttons = actions->terminalButtons(s);
    auto *left = new QWidget, *right = new QWidget;
    auto *ll = new QHBoxLayout(left), *rl = new QHBoxLayout(right);
    for (QBoxLayout *l : {ll, rl}) {
        l->setContentsMargins(0, 0, 0, 0);
        l->setSpacing(2);
    }
    if (!buttons.isEmpty() && s->hostId != Host::LOCAL) ll->addWidget(buttons.takeFirst());
    ll->addStretch(1);
    rl->addStretch(1);
    for (QWidget *b : buttons) rl->addWidget(b);
    auto *title = new QLabel(s->view->windowTitle());
    title->setStyleSheet(Theme::css("color: @faint; font-size: 11px; padding: 0 8px;"));
    title->setAlignment(Qt::AlignCenter);
    title->setMinimumWidth(0);
    connect(s->view, &QWidget::windowTitleChanged, title, &QLabel::setText);
    row->addWidget(left, 1);
    row->addWidget(title);
    row->addWidget(right, 1);
    auto *cover = new Connecting(model, s, nullptr);
    // The files of this computer and of the server, in place of the terminal; see-through too in ghost mode.
    QWidget *content = s->view;
    if (s->showFiles && s->hostId != Host::LOCAL) {
        if (!s->files) s->files = new FilesView(model, s);
        static_cast<FilesView *>(s->files.data())->setGhost(ghost);
        content = s->files;
        s->view->hide();
    }
    content->show();
    auto *sl = new Slot(content, bar, cover, synced, ghost);
    // A menu and file drops, once per terminal: the view outlives the console.
    if (!s->view->property("wired").toBool()) {
        s->view->setProperty("wired", true);
        Model *m = model;
        Actions *a = actions;
        connect(s->view, &TerminalView::menuRequested, m, [m, a, s](const QPoint &at) {
            if (!m->sessions().contains(s)) return;
            QMenu *menu = a->terminalMenu(s);
            menu->exec(at);
            menu->deleteLater();
        });
        connect(s->view, &TerminalView::filesDropped, m, [a, s](const QStringList &files) { a->upload(s, files); });
        connect(s->view, &TerminalView::nameDoubleClicked, m, [a, m, s](const QString &name, const QString &command, const QPoint &at) {
            if (m->sessions().contains(s)) a->openName(s, name, command, at);
        });
    }
    cover->refresh();
    return sl;
}

// ── Activity log ──

ActivityLog::ActivityLog(Model *model, QWidget *parent) : QWidget(parent), model(model) {
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);
    auto *head = new QWidget;
    head->setFixedHeight(28);
    head->setObjectName("logHead");
    head->setAttribute(Qt::WA_StyledBackground);
    head->setStyleSheet(Theme::css("#logHead { background: @veilHead; border-top-left-radius: 5px; border-top-right-radius: 5px; }"
                        "#logHead QPushButton { border: none; border-radius: 0; background: transparent; color: @faint;"
                        " min-height: 28px; max-height: 28px; padding: 0 12px; border-right: 1px solid @border; }"
                        "#logHead QPushButton:checked { background: @veilDeep; color: @text; font-weight: bold; }"));
    auto *row = new QHBoxLayout(head);
    row->setContentsMargins(6, 0, 4, 0);
    row->setSpacing(0);
    for (int i = 0; i < 3; i++) {
        auto *b = new QPushButton;
        b->setCheckable(true);
        b->setCursor(Qt::PointingHandCursor);
        connect(b, &QPushButton::clicked, this, [this, i] {
            setOpened(true);
            choose(i);
        });
        tabs << b;
        row->addWidget(b);
    }
    tabs[0]->setText(T("log.tab.terminal"));
    tabs[1]->setText(T("log.tab.tasks"));
    row->addStretch(1);
    clear = Ui::action("trash", T("log.clear"), [this] {
        this->model->clearEndedTasks();
    });
    row->addWidget(clear);
    toggle = Ui::action("up", T("log.open"), [this] { setOpened(!isOpen); });
    row->addWidget(toggle);
    col->addWidget(head);

    body = new QWidget;
    bodyLayout = new QVBoxLayout(body);
    bodyLayout->setContentsMargins(0, 0, 0, 0);
    tasks = grid({T("log.col.start"), T("log.col.end"), T("common.host"), T("log.col.description"), T("common.status")});
    tasks->setStyleSheet(Theme::css("QTableWidget { border: none; }"));
    for (int i = 0; i < 5; i++) tasks->setColumnWidth(i, QList<int>{150, 150, 150, 360, 320}[i]);
    tasks->horizontalHeader()->setStretchLastSection(true);
    sessions = grid({T("sessions.col.session"), T("sessions.col.destination"), T("common.status"), T("sessions.col.started")});
    sessions->setStyleSheet(Theme::css("QTableWidget { border: none; }"));
    sessions->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    connect(sessions, &QTableWidget::cellDoubleClicked, this, [this](int r, int) {
        int i = sessions->item(r, 0)->data(Qt::UserRole).toInt();
        if (i >= 0 && i < this->model->sessions().size()) this->model->showSession(this->model->sessions()[i]);
    });
    bodyLayout->addWidget(tasks);
    bodyLayout->addWidget(sessions);
    col->addWidget(body, 1);

    connect(model, &Model::tasksChanged, this, &ActivityLog::fillTasks);
    connect(model, &Model::sessionsChanged, this, &ActivityLog::fillSessions);
    connect(model, &Model::sessionChanged, this, &ActivityLog::fillSessions);
    connect(model->backend(), &Backend::data, this, [this](const QString &id, const QString &text) {
        if (shell && id == shellId) shell->write(text);
    });
    connect(model->backend(), &Backend::state, this, [this](const QString &id, const QString &state, const QString &) {
        // The local shell exited: back to the tasks; the next click starts a new one.
        if (!shell || id != shellId || state != "CLOSED") return;
        shell->deleteLater();
        shell = nullptr;
        shellId.clear();
        emit shellGone();
        if (current == 0) choose(1);
    });
    fillTasks();
    fillSessions();
    choose(1);
    setOpened(false);
}

TerminalView *ActivityLog::lendShell() {
    if (!shell) startShell();
    bodyLayout->removeWidget(shell);
    shell->setParent(nullptr);
    lent = true;
    if (current == 0) choose(1);
    return shell;
}

void ActivityLog::takeBackShell() {
    lent = false;
    if (!shell) return;
    shell->setGhost(false);
    shell->setParent(body);
    bodyLayout->addWidget(shell);
    shell->setVisible(current == 0);
}

void ActivityLog::setOpened(bool open) {
    isOpen = open;
    body->setVisible(open);
    toggle->setIconName(open ? "down" : "up");
    toggle->setToolTip(open ? T("log.collapse") : T("log.open"));
    clear->setVisible(open && current == 1);
    setMaximumHeight(open ? QWIDGETSIZE_MAX : 28);
    setMinimumHeight(open ? 120 : 28);
    emit openedChanged(open);
}

void ActivityLog::choose(int tab) {
    current = tab;
    for (int i = 0; i < tabs.size(); i++) tabs[i]->setChecked(i == tab);
    tasks->setVisible(tab == 1);
    sessions->setVisible(tab == 2);
    clear->setVisible(isOpen && tab == 1);
    if (tab == 0) {
        if (lent) {
            current = 1;
            return choose(1);
        }
        if (!shell) startShell();
        shell->show();
        shell->setFocus();
    } else if (shell) {
        shell->hide();
    }
}

void ActivityLog::startShell() {
    shell = new TerminalView;
    shell->setTypesCommands(model->typesCommands());
    connect(model, &Model::typesCommandsChanged, shell, &TerminalView::setTypesCommands);
    bodyLayout->addWidget(shell);
    connect(shell, &TerminalView::input, this, [this](const QByteArray &b) {
        if (!shellId.isEmpty()) model->backend()->request("session.input", {{"session", shellId}, {"data", QString::fromUtf8(b)}});
    });
    connect(shell, &TerminalView::resized, this, [this](int c, int r) {
        if (!shellId.isEmpty()) model->backend()->request("session.resize", {{"session", shellId}, {"cols", c}, {"rows", r}});
    });
    connect(shell, &TerminalView::menuRequested, this, [this](const QPoint &at) {
        QMenu m;
        m.addAction(Icons::icon("copy", 13, Theme::text), T("terminal.copy"), shell, &TerminalView::copySelection)->setEnabled(shell->hasSelection());
        m.addAction(Icons::icon("paste", 13, Theme::text), T("terminal.paste"), shell, &TerminalView::pasteClipboard);
        m.exec(at);
    });
    TerminalView *t = shell;
    QTimer::singleShot(0, this, [this, t] {
        if (shell != t) return;
        model->request(this, "session.open", {{"local", true}, {"cols", t->columns()}, {"rows", t->lines()}}, [this, t](const QJsonValue &v) {
            if (shell != t) return;
            shellId = v.toObject()["session"].toString();
            model->backend()->request("session.resize", {{"session", shellId}, {"cols", t->columns()}, {"rows", t->lines()}});
        });
    });
}

void ActivityLog::fillTasks() {
    tasks->setRowCount(int(model->tasks().size()));
    for (int r = 0; r < model->tasks().size(); r++) {
        const Task &t = model->tasks()[r];
        bool error = t.outcome == Task::Outcome::Error;
        auto put = [&](int c, const QString &text) {
            QTableWidgetItem *i = cell(text);
            if (error) i->setForeground(Theme::critical);
            tasks->setItem(r, c, i);
        };
        put(0, Ui::dateTime(t.start));
        put(1, t.end ? Ui::dateTime(t.end) : QString());
        put(2, t.target);
        put(3, t.text);
        put(4, "");
        QLabel *chip = t.outcome == Task::Outcome::Running ? Ui::chip("info", T("log.running")) : t.outcome == Task::Outcome::Ok ? Ui::chip("ok", T("log.ok"))
                     : Ui::chip("error", t.detail.isEmpty() ? T("session.state.error") : t.detail);
        chip->setToolTip(t.detail);
        tasks->setCellWidget(r, 4, centred(chip));
    }
    tasks->scrollToBottom();
}

void ActivityLog::fillSessions() {
    tabs[2]->setText(T("log.tab.sessions", int(model->sessions().size())));
    sessions->setRowCount(int(model->sessions().size()));
    for (int r = 0; r < model->sessions().size(); r++) {
        Session *s = model->sessions()[r];
        const Host *h = model->host(s->hostId);
        QTableWidgetItem *first = cell(s->title);
        first->setData(Qt::UserRole, r);
        sessions->setItem(r, 0, first);
        sessions->setItem(r, 1, cell(h ? h->destination() : QString()));
        using State = Session::State;
        QString tone = s->state == State::Connected ? "ok" : s->state == State::Error ? "error" : s->state == State::Closed ? "neutral" : "info";
        sessions->setItem(r, 2, cell(""));
        sessions->setCellWidget(r, 2, centred(Ui::chip(tone, Ui::sessionStateText(s->state))));
        sessions->setItem(r, 3, cell(Ui::dateTime(s->started)));
    }
}
