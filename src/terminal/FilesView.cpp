#include "terminal/FilesView.h"
#include "app/core/Model.h"
#include "app/tools/Texts.h"
#include "terminal/TerminalView.h"
#include "widgets/look/Icons.h"
#include "widgets/look/Theme.h"
#include "widgets/parts/Chrome.h"
#include "widgets/parts/Ui.h"
#include <QApplication>
#include <QCheckBox>
#include <QDateTime>
#include <QDir>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QPointer>
#include <QPushButton>
#include <QShortcut>
#include <QSplitter>
#include <QTimer>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
/** What a drag between the two sides carries: "local" or "remote", then one path per line. */
const QString MIME = "application/x-lemuri-files";

enum Role { Path = Qt::UserRole, Folder, Bytes, Changed };

QString sizeText(qint64 bytes) {
    if (bytes < 1024) return QString::number(bytes) + " B";
    return Ui::measure(bytes / 1024);
}

QString joined(const QString &folder, const QString &name) {
    return folder.endsWith('/') ? folder + name : folder + "/" + name;
}

/** The folder above a server path; "/" stays "/". */
QString parentOf(const QString &path) {
    int at = path.lastIndexOf('/');
    return at <= 0 ? QString("/") : path.left(at);
}

QString lastName(const QString &path) {
    return path.mid(path.lastIndexOf('/') + 1);
}

/** A name for a new file or folder: not empty, no slash, not . or .. */
QString badName(const QString &n) {
    QString t = n.trimmed();
    if (t.isEmpty() || t == "." || t == ".." || t.contains('/')) return T("files.badName");
    return {};
}

/** Folders first, then by the column, names without regard to case. */
class Entry : public QTreeWidgetItem {
public:
    using QTreeWidgetItem::QTreeWidgetItem;
    bool operator<(const QTreeWidgetItem &o) const override {
        bool a = data(0, Folder).toBool(), b = o.data(0, Folder).toBool();
        Qt::SortOrder order = treeWidget() ? treeWidget()->header()->sortIndicatorOrder() : Qt::AscendingOrder;
        // Folders stay on top whichever way the column is sorted.
        if (a != b) return order == Qt::AscendingOrder ? a : b;
        int c = treeWidget() ? treeWidget()->sortColumn() : 0;
        if (c == 1 && !a) return data(0, Bytes).toLongLong() < o.data(0, Bytes).toLongLong();
        if (c == 2) return data(0, Changed).toLongLong() < o.data(0, Changed).toLongLong();
        return text(0).compare(o.text(0), Qt::CaseInsensitive) < 0;
    }
};
}  // namespace

/** One side: this computer or the server. */
class FilesSide : public QWidget {
public:
    FilesSide(FilesView *owner, bool remote);
    /** Lists `folder` ("" on the server is its home folder). */
    void open(const QString &folder);
    void reload() { open(path); }
    QString folder() const { return path; }
    bool remote;
    /** Ready to take files: the folder is known (on the server, it has been listed). */
    bool ready() const { return !path.isEmpty(); }

private:
    class List;
    void show(const QString &at, const QJsonArray &entries);
    void fail(const QString &why);
    void activate(QTreeWidgetItem *item);
    QStringList chosen() const;
    void sendChosen();
    void newFolder();
    void rename();
    void remove();
    void menu(const QPoint &at);
    bool connected() const;

    FilesView *owner;
    QString path;
    QLineEdit *where;
    QTreeWidget *list;
    QLabel *status;
    QPushButton *send;
    int asked = 0;
};

/** The list, with drags to the other side and drops from it (or, on the server's, files from the desktop). */
class FilesSide::List : public QTreeWidget {
public:
    explicit List(FilesSide *side) : side(side) {
        setDragEnabled(true);
        setAcceptDrops(true);
        setDropIndicatorShown(false);
        setDragDropMode(QAbstractItemView::DragDrop);
    }

protected:
    QMimeData *mimeData(const QList<QTreeWidgetItem *> &items) const override {
        QStringList paths;
        for (QTreeWidgetItem *i : items) paths << i->data(0, Path).toString();
        auto *m = new QMimeData;
        m->setData(MIME, ((side->remote ? "remote\n" : "local\n") + paths.join('\n')).toUtf8());
        return m;
    }
    QStringList mimeTypes() const override { return {MIME, "text/uri-list"}; }
    Qt::DropActions supportedDropActions() const override { return Qt::CopyAction; }

    /** What a drop here would copy: from the other side, or local files dropped on the server's list. */
    QStringList incoming(const QMimeData *m) const {
        if (m->hasFormat(MIME)) {
            QStringList l = QString::fromUtf8(m->data(MIME)).split('\n');
            QString from = l.takeFirst();
            return (from == "remote") != side->remote ? l : QStringList();
        }
        QStringList files;
        if (side->remote && m->hasUrls())
            for (const QUrl &u : m->urls())
                if (u.isLocalFile()) files << u.toLocalFile();
        return files;
    }
    void dragEnterEvent(QDragEnterEvent *e) override {
        if (incoming(e->mimeData()).isEmpty()) e->ignore();
        else e->acceptProposedAction();
    }
    void dragMoveEvent(QDragMoveEvent *e) override {
        if (incoming(e->mimeData()).isEmpty()) e->ignore();
        else e->acceptProposedAction();
    }
    void dropEvent(QDropEvent *e) override {
        QStringList files = incoming(e->mimeData());
        if (files.isEmpty()) return e->ignore();
        e->acceptProposedAction();
        if (side->remote) side->owner->upload(files);
        else side->owner->download(files);
    }

private:
    FilesSide *side;
};

FilesSide::FilesSide(FilesView *owner, bool remote) : remote(remote), owner(owner) {
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(8, 6, 8, 6);
    col->setSpacing(6);

    // Whose files, and the button that copies the chosen ones to the other side.
    auto *head = new QHBoxLayout;
    head->setSpacing(6);
    auto *icon = new QLabel;
    icon->setPixmap(Icons::pixmap(remote ? "server" : "monitor", 16, remote ? Theme::ok : Theme::accent, 2, devicePixelRatioF()));
    head->addWidget(icon);
    const Host *h = owner->model->host(owner->session->hostId);
    auto *title = new QLabel(remote ? (h ? h->name : owner->session->title) : T("files.here"));
    title->setStyleSheet(Theme::css("font-weight: bold;"));
    head->addWidget(title);
    if (remote && h) head->addWidget(Ui::faint(h->destination()));
    head->addStretch(1);
    send = remote ? Ui::button(T("files.download"), "download", [this] { sendChosen(); })
                  : Ui::button(T("files.upload"), "upload", [this] { sendChosen(); });
    send->setToolTip(T(remote ? "files.download.tip" : "files.upload.tip"));
    send->setEnabled(false);
    head->addWidget(send);
    col->addLayout(head);

    // Where: up, home, read again, the path (typed and Enter to go there), a new folder, delete.
    auto *nav = new QHBoxLayout;
    nav->setSpacing(2);
    nav->addWidget(Ui::action("up", T("files.up"), [this] {
        if (ready()) open(this->remote ? parentOf(path) : QFileInfo(path).dir().absolutePath());
    }));
    nav->addWidget(Ui::action("home", T("files.home"), [this] { open(this->remote ? QString() : QDir::homePath()); }));
    nav->addWidget(Ui::action("refresh", T("files.refresh"), [this] { reload(); }));
    where = new QLineEdit;
    where->setStyleSheet(Theme::css("font-family: 'DejaVu Sans Mono'; font-size: 11px;"));
    connect(where, &QLineEdit::returnPressed, this, [this] { open(where->text().trimmed()); });
    nav->addSpacing(4);
    nav->addWidget(where, 1);
    nav->addSpacing(4);
    nav->addWidget(Ui::action("plus", T("files.newFolder"), [this] { newFolder(); }));
    nav->addWidget(Ui::action("edit", T("files.rename"), [this] { rename(); }));
    nav->addWidget(Ui::action("trash", T("files.delete"), [this] { remove(); }));
    col->addLayout(nav);

    list = new List(this);
    list->setColumnCount(3);
    list->setHeaderLabels({T("files.col.name"), T("files.col.size"), T("files.col.modified")});
    list->setRootIsDecorated(false);
    list->setSelectionMode(QAbstractItemView::ExtendedSelection);
    list->setUniformRowHeights(true);
    list->setSortingEnabled(true);
    list->sortByColumn(0, Qt::AscendingOrder);
    list->setContextMenuPolicy(Qt::CustomContextMenu);
    list->setIconSize(QSize(16, 16));
    list->header()->setStretchLastSection(false);
    list->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
    connect(list, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *i) { activate(i); });
    connect(list, &QTreeWidget::itemSelectionChanged, this, [this] { send->setEnabled(!chosen().isEmpty() && this->owner->remoteReady()); });
    connect(list, &QWidget::customContextMenuRequested, this, &FilesSide::menu);
    col->addWidget(list, 1);

    status = Ui::faint("");
    col->addWidget(status);

    auto key = [this](const QKeySequence &k, std::function<void()> f) {
        auto *s = new QShortcut(k, list);
        s->setContext(Qt::WidgetShortcut);
        connect(s, &QShortcut::activated, this, f);
    };
    key(Qt::Key_Backspace, [this] {
        if (ready()) open(this->remote ? parentOf(path) : QFileInfo(path).dir().absolutePath());
    });
    key(Qt::Key_Delete, [this] { remove(); });
    key(Qt::Key_F2, [this] { rename(); });
    key(Qt::Key_F5, [this] { reload(); });
}

bool FilesSide::connected() const {
    Session *s = owner->session;
    return s->state == Session::State::Connected && !s->backend.isEmpty();
}

void FilesSide::open(const QString &folder) {
    if (!remote) {
        QFileInfo f(folder.isEmpty() ? QDir::homePath() : folder);
        if (!f.isDir()) {
            status->setText(T("files.notAFolder", folder));
            where->setText(path);
            return;
        }
        QJsonArray entries;
        QDir d(f.absoluteFilePath());
        for (const QFileInfo &i : d.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System))
            entries << QJsonObject{{"name", i.fileName()}, {"dir", i.isDir()}, {"link", i.isSymLink()},
                                   {"size", double(i.size())}, {"mtime", double(i.lastModified().toSecsSinceEpoch())}};
        show(d.absolutePath(), entries);
        return;
    }
    if (!connected()) {
        list->clear();
        status->setText(T("files.notConnected"));
        return;
    }
    status->setText(T("files.loading"));
    int ask = ++asked;
    QPointer<FilesSide> self(this);
    owner->model->request("sftp.list", {{"session", owner->session->backend}, {"path", folder}}, [self, ask](const QJsonValue &v) {
        if (!self || ask != self->asked) return;
        QJsonObject r = v.toObject();
        self->show(r["path"].toString(), r["entries"].toArray());
    }, [self, ask, folder](const QString &why) {
        if (!self || ask != self->asked) return;
        self->fail(folder + ": " + why);
    });
}

void FilesSide::show(const QString &at, const QJsonArray &entries) {
    bool moved = at != path;
    path = at;
    where->setText(at);
    // The selection stays across a reading of the same folder.
    QSet<QString> was;
    if (!moved)
        for (QTreeWidgetItem *i : list->selectedItems()) was << i->text(0);
    list->setSortingEnabled(false);
    list->clear();
    int shown = 0;
    for (const QJsonValue &v : entries) {
        QJsonObject e = v.toObject();
        QString name = e["name"].toString();
        if (!owner->showHidden && name.startsWith('.')) continue;
        bool dir = e["dir"].toBool();
        qint64 bytes = qint64(e["size"].toDouble()), mtime = qint64(e["mtime"].toDouble());
        auto *i = new Entry(list);
        i->setText(0, name);
        i->setIcon(0, Icons::icon(dir ? "folder" : "file", 16, dir ? Theme::vaultIcon : Theme::faint, 1.6));
        i->setText(1, dir ? QString() : sizeText(bytes));
        i->setText(2, mtime > 0 ? QDateTime::fromSecsSinceEpoch(mtime).toString("dd/MM/yyyy HH:mm") : QString());
        i->setTextAlignment(1, Qt::AlignRight | Qt::AlignVCenter);
        i->setData(0, Path, joined(at, name));
        i->setData(0, Folder, dir);
        i->setData(0, Bytes, bytes);
        i->setData(0, Changed, mtime);
        if (e["link"].toBool()) i->setToolTip(0, T("files.link", name));
        if (was.contains(name)) i->setSelected(true);
        shown++;
    }
    list->setSortingEnabled(true);
    status->setText(T("files.items", shown));
    send->setEnabled(!chosen().isEmpty() && owner->remoteReady());
}

void FilesSide::fail(const QString &why) {
    status->setText(why);
    where->setText(path);
}

QStringList FilesSide::chosen() const {
    QStringList l;
    for (QTreeWidgetItem *i : list->selectedItems()) l << i->data(0, Path).toString();
    return l;
}

void FilesSide::activate(QTreeWidgetItem *item) {
    if (item->data(0, Folder).toBool()) {
        open(item->data(0, Path).toString());
        return;
    }
    // A file: copied to the other side, as with the button.
    QStringList one{item->data(0, Path).toString()};
    if (remote) owner->download(one);
    else owner->upload(one);
}

void FilesSide::sendChosen() {
    QStringList l = chosen();
    if (l.isEmpty()) return;
    if (remote) owner->download(l);
    else owner->upload(l);
}

void FilesSide::newFolder() {
    if (!ready()) return;
    auto name = Ui::name(this, T("files.newFolder"), T("files.newFolder.label", path), "", T("files.newFolder.create"), badName);
    if (!name) return;
    QString target = joined(path, name->trimmed());
    if (!remote) {
        if (QFileInfo::exists(target)) Ui::warning(this, T("files.newFolder"), T("files.exists", name->trimmed()));
        else if (!QDir().mkdir(target)) Ui::warning(this, T("files.newFolder"), T("files.cannot", target));
        reload();
        return;
    }
    QPointer<FilesSide> self(this);
    owner->model->request("sftp.mkdir", {{"session", owner->session->backend}, {"path", target}}, [self](const QJsonValue &) {
        if (self) self->reload();
    }, [self](const QString &why) {
        if (self) Ui::warning(self, T("files.newFolder"), why);
    });
}

void FilesSide::rename() {
    QList<QTreeWidgetItem *> l = list->selectedItems();
    if (l.size() != 1) return;
    QString from = l[0]->data(0, Path).toString(), old = l[0]->text(0);
    auto name = Ui::name(this, T("files.rename"), T("files.rename.label", old), old, T("files.rename"), badName);
    if (!name || name->trimmed() == old) return;
    QString to = joined(path, name->trimmed());
    if (!remote) {
        if (QFileInfo::exists(to)) Ui::warning(this, T("files.rename"), T("files.exists", name->trimmed()));
        else if (!QFile::rename(from, to)) Ui::warning(this, T("files.rename"), T("files.cannot", from));
        reload();
        return;
    }
    QPointer<FilesSide> self(this);
    owner->model->request("sftp.rename", {{"session", owner->session->backend}, {"from", from}, {"to", to}}, [self](const QJsonValue &) {
        if (self) self->reload();
    }, [self](const QString &why) {
        if (self) Ui::warning(self, T("files.rename"), why);
    });
}

void FilesSide::remove() {
    QStringList paths = chosen();
    if (paths.isEmpty()) return;
    QStringList names;
    for (const QString &p : paths) names << "\"" + lastName(p) + "\"";
    QString what = paths.size() == 1 ? names[0] : T("files.items", int(paths.size()));
    if (!remote) {
        // Here, to the trash: it can be taken back.
        if (!Ui::confirm(this, T("files.delete"), T("files.delete.local", what), T("files.delete.toTrash"), true)) return;
        QStringList failed;
        for (const QString &p : paths)
            if (!QFile::moveToTrash(p)) failed << lastName(p);
        if (!failed.isEmpty()) Ui::warning(this, T("files.delete"), T("files.delete.failed", failed.join(", ")));
        reload();
        return;
    }
    const Host *h = owner->model->host(owner->session->hostId);
    if (!Ui::confirm(this, T("files.delete"), T("files.delete.remote", what, h ? h->name : QString()), T("files.delete"), true)) return;
    QJsonArray l;
    for (const QString &p : paths) l << p;
    QPointer<FilesSide> self(this);
    owner->model->request("sftp.remove", {{"session", owner->session->backend}, {"paths", l}}, [self](const QJsonValue &) {
        if (self) self->reload();
    }, [self](const QString &why) {
        if (!self) return;
        Ui::warning(self, T("files.delete"), why);
        self->reload();
    });
}

void FilesSide::menu(const QPoint &at) {
    QTreeWidgetItem *under = list->itemAt(at);
    QStringList l = chosen();
    QMenu m(this);
    auto item = [&](const QString &text, const QString &icon, std::function<void()> f) {
        QAction *a = m.addAction(Icons::icon(icon, 14, Theme::text), text);
        connect(a, &QAction::triggered, this, f);
        return a;
    };
    if (under && under->data(0, Folder).toBool() && l.size() == 1)
        item(T("files.open"), "folder-open", [this, under] { activate(under); });
    if (!l.isEmpty()) {
        item(T(remote ? "files.download" : "files.upload"), remote ? "download" : "upload", [this] { sendChosen(); })
            ->setEnabled(owner->remoteReady());
        item(T("files.rename"), "edit", [this] { rename(); })->setEnabled(l.size() == 1);
        item(T("files.delete"), "trash", [this] { remove(); });
        m.addSeparator();
    }
    item(T("files.newFolder"), "plus", [this] { newFolder(); });
    item(T("files.refresh"), "refresh", [this] { reload(); });
    m.exec(list->viewport()->mapToGlobal(at));
}

// ── The view ──

FilesView::FilesView(Model *model, Session *session, QWidget *parent) : QWidget(parent), model(model), session(session) {
    setAttribute(Qt::WA_StyledBackground);
    setObjectName("files");
    auto *col = new QVBoxLayout(this);
    col->setContentsMargins(0, 0, 0, 0);
    col->setSpacing(0);

    // A thin bar under the band of the slot: the hint and the hidden files.
    auto *bar = new QWidget;
    bar->setObjectName("filesBar");
    bar->setAttribute(Qt::WA_StyledBackground);
    bar->setFixedHeight(22);
    auto *row = new QHBoxLayout(bar);
    row->setContentsMargins(10, 0, 10, 0);
    row->setSpacing(14);
    auto *hint = Ui::faint(T("files.hint"));
    hint->setStyleSheet(hint->styleSheet() + "font-size: 11px;");
    row->addWidget(hint);
    auto *hidden = new QCheckBox(T("files.hidden"));
    hidden->setStyleSheet("font-size: 11px;");
    connect(hidden, &QCheckBox::toggled, this, [this](bool on) {
        showHidden = on;
        reload();
    });
    row->addWidget(hidden);
    row->addStretch(1);
    col->addWidget(bar);

    auto *split = new QSplitter(Qt::Horizontal);
    split->setHandleWidth(2);
    split->setChildrenCollapsible(false);
    local = new FilesSide(this, false);
    remote = new FilesSide(this, true);
    split->addWidget(local);
    split->addWidget(remote);
    col->addWidget(split, 1);
    setGhost(false);

    local->open(QDir::homePath());
    // The server side starts where the shell is.
    QPointer<FilesView> self(this);
    auto start = [self](const QString &folder) {
        if (self) self->remote->open(folder);
    };
    if (session->view) session->view->currentFolder(start);
    else start({});

    // Once connected (again), the server side is read; a transfer that ends reads both sides again.
    connect(model, &Model::sessionChanged, this, [this](Session *s) {
        if (s == this->session && s->state == Session::State::Connected && !remote->ready()) remote->open({});
    });
    for (const Transfer &t : model->transfers())
        if (t.finished) finished << t.id;
    connect(model, &Model::transfersChanged, this, [this] {
        bool fresh = false;
        for (const Transfer &t : this->model->transfers())
            if (t.finished && !finished.contains(t.id)) {
                finished << t.id;
                fresh = true;
            }
        if (!fresh || pending) return;
        pending = true;
        QTimer::singleShot(250, this, [this] {
            pending = false;
            reload();
        });
    });
}

void FilesView::setGhost(bool on) {
    if (ghost == on) return;
    ghost = on;
    // All the colours here, on the view: a sheet of a child would win over these. The ghost window has not the main
    // window's sheet: the view brings it along.
    setStyleSheet((on ? Ui::styleSheet() : QString()) + Theme::css(on ? "#files { background: transparent; }"
                                  "#filesBar { background: rgba(0,0,0,0.30); }"
                                  "QTreeWidget { background: transparent; border: 1px solid rgba(255,255,255,0.10); }"
                                  "QHeaderView::section { background: rgba(255,255,255,0.07); }"
                                  "QLineEdit { background: rgba(0,0,0,0.30); border-color: rgba(255,255,255,0.14); }"
                                  "QLineEdit:focus { border-color: @ghostAccent; }"
                                  "QSplitter::handle { background: rgba(255,255,255,0.14); }"
                                  "QSplitter::handle:hover { background: rgba(255,255,255,0.40); }"
                                : "#files { background: @bg; } #filesBar { background: @terminalActions; }"
                                  "QSplitter::handle { background: @border; } QSplitter::handle:hover { background: @edge; }"));
    // The lists paint their area with the palette's base: see-through only without it.
    for (QAbstractItemView *v : findChildren<QAbstractItemView *>()) {
        QPalette p = v->palette();
        p.setColor(QPalette::Base, on ? QColor(Qt::transparent) : Theme::bg);
        v->setPalette(p);
        v->viewport()->setAutoFillBackground(!on);
    }
}

bool FilesView::remoteReady() const {
    return remote->ready() && session->state == Session::State::Connected && !session->backend.isEmpty();
}

void FilesView::reload() {
    local->reload();
    if (remote->ready()) remote->reload();
}

void FilesView::upload(const QStringList &paths) {
    if (paths.isEmpty()) return;
    if (!remoteReady()) {
        Ui::warning(this, T("files.upload"), T("files.notConnected"));
        return;
    }
    QString folder = remote->folder(), backendId = session->backend;
    QJsonArray list;
    for (const QString &p : paths) list << p;
    QPointer<FilesView> self(this);
    // What would be replaced is asked once; otherwise it starts at once, as a drag in a file manager.
    model->request("sftp.existing", {{"session", backendId}, {"folder", folder}, {"files", list}}, [self, folder, list, backendId](const QJsonValue &v) {
        if (!self) return;
        QJsonArray exist = v.toArray();
        QStringList names;
        for (const QJsonValue &x : exist) names << x.toString();
        if (!names.isEmpty() && !Ui::confirm(self, T("files.upload"), T("files.overwrite.there", names.join(", "), folder), T("transfer.overwrite"), true))
            return;
        self->model->request("sftp.upload", {{"session", backendId}, {"folder", folder}, {"files", list}, {"known", exist}}, nullptr,
                             [self](const QString &why) {
            if (self) Ui::warning(self, T("files.upload"), why);
        });
    }, [self](const QString &why) {
        if (self) Ui::warning(self, T("files.upload"), why);
    });
}

void FilesView::download(const QStringList &paths) {
    if (paths.isEmpty() || !remoteReady()) return;
    QString into = local->folder();
    QStringList replaced;
    for (const QString &p : paths) {
        QFileInfo here(QDir(into).filePath(lastName(p)));
        if (here.exists() && !here.isDir()) replaced << here.fileName();
    }
    // Files already here: asked once for all. Folders: the backend asks, knowing how many files would be replaced.
    if (!replaced.isEmpty() && !Ui::confirm(this, T("files.download"), T("files.overwrite.here", replaced.join(", "), into), T("transfer.overwrite"), true))
        return;
    QPointer<FilesView> self(this);
    for (const QString &p : paths)
        model->request("sftp.download", {{"session", session->backend}, {"path", p}, {"into", into}, {"replace", replaced.contains(lastName(p))}}, nullptr,
                       [self](const QString &why) {
            if (self) Ui::warning(self, T("files.download"), why);
        });
}
