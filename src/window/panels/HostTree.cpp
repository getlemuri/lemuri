#include "window/panels/HostTree.h"

#include "app/core/Model.h"
#include "app/tools/Texts.h"
#include "widgets/look/Icons.h"
#include "widgets/look/Theme.h"
#include "widgets/parts/Ui.h"
#include "window/main/Actions.h"

#include <QDrag>
#include <QHelpEvent>
#include <QJsonObject>
#include <QMap>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QScrollBar>
#include <QToolTip>

namespace {
constexpr int ROW = 28, INDENT = 14, SIDE = 6, CHEVRON = 17, HOST_SHIFT = 8, METER = 40;
const char *MIME = "application/x-lemuri-node";

QString folderName(const QString &p) {
    return p.mid(p.lastIndexOf('/') + 1);
}

QString parentOf(const QString &p) {
    int i = p.lastIndexOf('/');
    return i < 0 ? QString() : p.left(i);
}

bool inside(const QString &p, const QString &folder) {
    return folder.isEmpty() || p == folder || p.startsWith(folder + "/");
}
}  // namespace

HostTree::HostTree(Model *model, Actions *actions, QWidget *parent) : QTreeWidget(parent), model(model), actions(actions) {
    setHeaderHidden(true);
    setIndentation(0);
    setRootIsDecorated(false);
    setUniformRowHeights(true);
    setFrameShape(QFrame::NoFrame);
    setMouseTracking(true);
    setFocusPolicy(Qt::StrongFocus);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DragDrop);
    // Everything is drawn in drawRow: the view only gives the rows and the scrolling.
    setStyleSheet(Theme::css("QTreeWidget { background: transparent; border: none; outline: none; }"
                  "QTreeWidget::item { height: 28px; }"));
    viewport()->setAutoFillBackground(false);
    setViewportMargins(0, 2, 0, 6);
    connect(model, &Model::hostsChanged, this, &HostTree::rebuild);
    connect(model, &Model::sessionsChanged, this, &HostTree::rebuild);
    connect(model, &Model::sessionChanged, this, &HostTree::rebuild);
    connect(model, &Model::metricsChanged, viewport(), qOverload<>(&QWidget::update));
    connect(model, &Model::positionChanged, this, &HostTree::syncSelection);
    connect(this, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *i) {
        if (!syncing && filter.isEmpty()) closed.remove(i->data(0, KEY).toString());
    });
    connect(this, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *i) {
        if (!syncing && filter.isEmpty()) closed.insert(i->data(0, KEY).toString());
    });
    rebuild();
}

void HostTree::setFilter(const QString &text) {
    filter = text.trimmed().toLower();
    rebuild();
}

QTreeWidgetItem *HostTree::add(QTreeWidgetItem *parent, const QString &key, const QString &text) {
    auto *i = parent ? new QTreeWidgetItem(parent, {text}) : new QTreeWidgetItem(this, {text});
    i->setData(0, KEY, key);
    i->setSizeHint(0, QSize(-1, ROW));
    return i;
}

void HostTree::rebuild() {
    syncing = true;
    int scroll = verticalScrollBar()->value();
    clear();
    auto matches = [this](const Host &h) {
        return filter.isEmpty() || h.name.toLower().contains(filter) || h.address.toLower().contains(filter) || h.group.toLower().contains(filter);
    };
    QTreeWidgetItem *dc = add(nullptr, "dc", T("tree.datacenter"));
    // With the filter, the matching folders, those with matching hosts and their ancestors.
    QSet<QString> kept;
    if (!filter.isEmpty()) {
        QStringList found;
        for (const QString &f : model->folders())
            if (f.toLower().contains(filter)) found << f;
        for (const Host &h : model->hosts())
            if (matches(h)) found << h.group;
        for (QString p : found)
            for (; !p.isEmpty(); p = parentOf(p)) kept.insert(p.toLower());
    }
    QMap<QString, QTreeWidgetItem *> branches;
    branches.insert("", dc);
    for (const QString &f : model->folders()) {
        if (!filter.isEmpty() && !kept.contains(f.toLower())) continue;
        QTreeWidgetItem *parent = branches.value(parentOf(f).toLower());
        if (!parent) continue;
        branches.insert(f.toLower(), add(parent, "folder:" + f, folderName(f)));
    }
    QList<const Host *> sorted;
    for (const Host &h : model->hosts()) sorted << &h;
    std::sort(sorted.begin(), sorted.end(), [](const Host *a, const Host *b) { return a->name.compare(b->name, Qt::CaseInsensitive) < 0; });
    QMap<QString, QList<const Host *>> received;
    for (const Host *h : sorted) {
        if (!matches(*h)) continue;
        if (h->received) {
            received[h->fromContact.isEmpty() ? "?" : h->fromContact].append(h);
            continue;
        }
        if (QTreeWidgetItem *b = branches.value(h->group.toLower())) {
            QTreeWidgetItem *i = add(b, "host:" + h->id, h->name);
            i->setData(0, TALLY, model->connected(h->id));
        }
    }
    if (!received.isEmpty()) {
        QTreeWidgetItem *section = add(nullptr, "received", T("tree.received"));
        for (auto it = received.begin(); it != received.end(); ++it) {
            QTreeWidgetItem *from = add(section, "received:" + it.key(), it.key());
            for (const Host *h : it.value()) {
                QTreeWidgetItem *i = add(from, "host:" + h->id, h->name);
                i->setData(0, TALLY, model->connected(h->id));
            }
        }
    }
    // Open as before; with a filter every branch is open, or the results would be hidden.
    std::function<void(QTreeWidgetItem *)> open = [&](QTreeWidgetItem *i) {
        if (i->childCount()) i->setExpanded(!filter.isEmpty() || !closed.contains(i->data(0, KEY).toString()));
        for (int c = 0; c < i->childCount(); c++) open(i->child(c));
    };
    for (int t = 0; t < topLevelItemCount(); t++) open(topLevelItem(t));
    syncing = false;
    syncSelection();
    verticalScrollBar()->setValue(scroll);
}

void HostTree::expandFolders(bool open) {
    std::function<void(QTreeWidgetItem *)> walk = [&](QTreeWidgetItem *i) {
        for (int c = 0; c < i->childCount(); c++) {
            QTreeWidgetItem *x = i->child(c);
            if (!x->childCount()) continue;
            x->setExpanded(open);
            walk(x);
        }
    };
    for (int t = 0; t < topLevelItemCount(); t++) walk(topLevelItem(t));
}

/** The highlight follows the node in view, even after an ignored click. */
void HostTree::syncSelection() {
    const Position &p = model->position();
    QString key = p.kind == Position::Datacenter ? "dc" : p.kind == Position::Folder ? "folder:" + p.key
                : p.kind == Position::HostItem ? "host:" + p.key : p.kind == Position::Received ? "received" : QString();
    syncing = true;
    clearSelection();
    QTreeWidgetItemIterator it(this);
    for (; *it; ++it) {
        if ((*it)->data(0, KEY).toString() == key) {
            (*it)->setSelected(true);
            setCurrentItem(*it, 0, QItemSelectionModel::NoUpdate);
            break;
        }
    }
    syncing = false;
    viewport()->update();
}

void HostTree::choose(QTreeWidgetItem *item) {
    QString key = item->data(0, KEY).toString();
    if (key.startsWith("host:")) {
        QString id = key.mid(5);
        // A host with an open session brings you back to its shell; from the console a host without one gets one.
        if (Session *s = model->active().value(id)) {
            model->showSession(s);
            return;
        }
        if (model->position().item == "console") {
            model->showOrOpen(id);
            return;
        }
        model->select(Position::HostItem, id);
    } else if (key == "dc") {
        model->select(Position::Datacenter);
    } else if (key.startsWith("folder:")) {
        model->select(Position::Folder, key.mid(7));
    } else if (key.startsWith("received")) {
        model->select(Position::Received);
    }
    syncSelection();
}

int HostTree::depthOf(const QModelIndex &index) const {
    int depth = 0;
    for (QModelIndex i = index.parent(); i.isValid(); i = i.parent()) depth++;
    return depth;
}

void HostTree::drawRow(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const {
    QTreeWidgetItem *item = itemFromIndex(index);
    QString key = item->data(0, KEY).toString();
    bool isHost = key.startsWith("host:");
    const Host *h = isHost ? model->host(key.mid(5)) : nullptr;
    p->save();
    p->setRenderHint(QPainter::Antialiasing);

    QRectF pill = QRectF(option.rect).adjusted(SIDE, 1, -SIDE, -1);
    bool selected = item->isSelected();
    QColor fill = selected ? (hasFocus() ? Theme::treeFocusChoice : Theme::treeChoice)
                           : (index == hovered ? Theme::treeOn : Qt::transparent);
    if (index == target) fill = Theme::treeFocusChoice;
    if (fill.alpha()) {
        p->setPen(index == target ? QPen(Theme::dropTarget, 1) : Qt::NoPen);
        p->setBrush(fill);
        p->drawRoundedRect(pill, 7, 7);
    }
    double x = pill.left() + 4 + depthOf(index) * INDENT;
    double mid = pill.center().y();
    if (item->childCount() > 0) {
        QPainterPath arrow;
        if (item->isExpanded()) {
            arrow.moveTo(0, 0); arrow.lineTo(0, 1.4); arrow.lineTo(4, 5); arrow.lineTo(8, 1.4); arrow.lineTo(8, 0); arrow.lineTo(4, 3.6);
        } else {
            arrow.moveTo(0, 0); arrow.lineTo(1.4, 0); arrow.lineTo(5, 4); arrow.lineTo(1.4, 8); arrow.lineTo(0, 8); arrow.lineTo(3.6, 4);
        }
        arrow.closeSubpath();
        QRectF b = arrow.boundingRect();
        p->setPen(Qt::NoPen);
        p->setBrush(Theme::chevron);
        p->drawPath(arrow.translated(x + (CHEVRON - b.width()) / 2, mid - b.height() / 2));
    }
    x += CHEVRON;
    // Hosts have no arrow: the icon moves into part of that space.
    if (isHost) x -= HOST_SHIFT;

    QString icon = key == "dc" ? "datacenter" : key.startsWith("received") ? "user"
                 : key.startsWith("folder:") ? (item->isExpanded() ? "folder-open" : "folder") : "server";
    QColor iconColour = Theme::text;
    if (key.startsWith("folder:") || key.startsWith("received:")) iconColour = Theme::faint;
    else if (h) iconColour = h->reach == Host::Reach::Online ? Theme::ok : h->reach == Host::Reach::Offline ? Theme::critical : Theme::idle;
    Icons::paint(*p, icon, QRectF(x, mid - 8, 16, 16), iconColour);
    x += 16 + 8;

    double right = pill.right() - 10;
    if (h) {
        // The fullest disk: an icon and a bar whose colour speaks.
        QJsonObject m = model->metrics().value(h->id);
        QJsonObject d = Ui::fullestDisk(m);
        if (!d.isEmpty()) {
            double q = std::clamp(Ui::quota(d), 0.0, 1.0);
            QRectF track(right - METER, mid - 4, METER, 8);
            p->setPen(Qt::NoPen);
            p->setBrush(Theme::border);
            p->drawRoundedRect(track, 4, 4);
            if (q > 0) {
                p->setBrush(Ui::level(q));
                p->drawRoundedRect(QRectF(track.left(), track.top(), std::max(8.0, METER * q), 8), 4, 4);
            }
            right = track.left() - 5;
            Icons::paint(*p, "disk", QRectF(right - 16, mid - 8, 16, 16), Theme::faint);
            right -= 16 + 8;
        }
        int n = item->data(0, TALLY).toInt();
        if (n > 0) {
            QFont small = font();
            small.setPixelSize(10);
            small.setBold(true);
            QString number = QString::number(n);
            double w = std::max(18.0, QFontMetricsF(small).horizontalAdvance(number) + 10);
            QRectF badge(right - w, mid - 8, w, 16);
            p->setPen(Qt::NoPen);
            p->setBrush(QColor(124, 172, 255, 46));
            p->drawRoundedRect(badge, 8, 8);
            p->setFont(small);
            p->setPen(Theme::dropText);
            p->drawText(badge, Qt::AlignCenter, number);
            right = badge.left() - 6;
        }
    }
    QFont f = font();
    f.setPixelSize(14);
    f.setBold(!index.parent().isValid());
    p->setFont(f);
    p->setPen(Theme::text);
    QRectF textBox(x, pill.top(), right - x, pill.height());
    p->drawText(textBox, Qt::AlignVCenter | Qt::AlignLeft, QFontMetrics(f).elidedText(item->text(0), Qt::ElideRight, int(textBox.width())));
    p->restore();
}

bool HostTree::viewportEvent(QEvent *e) {
    if (e->type() == QEvent::ToolTip) {
        auto *h = static_cast<QHelpEvent *>(e);
        QTreeWidgetItem *i = itemAt(h->pos());
        QString key = i ? i->data(0, KEY).toString() : QString();
        const Host *host = key.startsWith("host:") ? model->host(key.mid(5)) : nullptr;
        if (!host) {
            QToolTip::hideText();
            return true;
        }
        // Over the disk bar: every disk; elsewhere what a double click does.
        QJsonObject m = model->metrics().value(host->id);
        bool overDisk = !Ui::fullestDisk(m).isEmpty() && h->pos().x() > viewport()->width() - SIDE - 10 - METER - 30;
        QToolTip::showText(h->globalPos(), overDisk ? Ui::disksTip(m) : T("tree.hostTip", host->destination()), this);
        return true;
    }
    return QTreeWidget::viewportEvent(e);
}

void HostTree::mouseMoveEvent(QMouseEvent *e) {
    QModelIndex i = indexAt(e->pos());
    if (i != hovered) {
        hovered = i;
        viewport()->update();
    }
    QTreeWidget::mouseMoveEvent(e);
}

void HostTree::leaveEvent(QEvent *e) {
    hovered = QModelIndex();
    viewport()->update();
    QTreeWidget::leaveEvent(e);
}

void HostTree::mousePressEvent(QMouseEvent *e) {
    QModelIndex i = indexAt(e->pos());
    if (i.isValid() && e->button() == Qt::LeftButton) {
        QTreeWidgetItem *item = itemFromIndex(i);
        double x = SIDE + 4 + depthOf(i) * INDENT;
        // A click on the chevron opens or closes the folder.
        if (item->childCount() > 0 && e->pos().x() >= x && e->pos().x() < x + CHEVRON) {
            item->setExpanded(!item->isExpanded());
            return;
        }
        QTreeWidget::mousePressEvent(e);
        choose(item);
        return;
    }
    QTreeWidget::mousePressEvent(e);
}

void HostTree::mouseDoubleClickEvent(QMouseEvent *e) {
    QTreeWidgetItem *item = itemAt(e->pos());
    if (!item) return;
    QString key = item->data(0, KEY).toString();
    if (key.startsWith("host:")) model->showOrOpen(key.mid(5));
    else item->setExpanded(!item->isExpanded());
}

void HostTree::keyPressEvent(QKeyEvent *e) {
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && currentItem()) {
        QString key = currentItem()->data(0, KEY).toString();
        if (key.startsWith("host:")) model->showOrOpen(key.mid(5));
        else choose(currentItem());
        return;
    }
    QTreeWidget::keyPressEvent(e);
    if (currentItem() && (e->key() == Qt::Key_Up || e->key() == Qt::Key_Down)) choose(currentItem());
}

void HostTree::contextMenuEvent(QContextMenuEvent *e) {
    QTreeWidgetItem *item = itemAt(e->pos());
    if (!item) return;
    QString key = item->data(0, KEY).toString();
    QMenu *m = key.startsWith("host:") ? actions->hostMenu(key.mid(5))
             : key.startsWith("folder:") ? actions->folderMenu(key.mid(7))
             : key == "dc" ? actions->datacenterMenu() : nullptr;
    if (!m) return;
    m->exec(e->globalPos());
    m->deleteLater();
}

// ── Drag and drop ──

QString HostTree::destination(QTreeWidgetItem *item) const {
    if (!item) return QString();
    QString key = item->data(0, KEY).toString();
    if (key == "dc") return QStringLiteral("");
    if (key.startsWith("folder:")) return key.mid(7);
    return QString();
}

bool HostTree::accept(const QString &d, const QString &dest) const {
    if (d.isEmpty() || dest.isNull()) return false;
    if (d.startsWith("host:")) {
        const Host *h = model->host(d.mid(5));
        return h && (h->received || h->group.compare(dest, Qt::CaseInsensitive) != 0);
    }
    QString c = d.mid(7);
    return dest != parentOf(c) && !inside(dest, c);
}

void HostTree::startDrag(Qt::DropActions) {
    QTreeWidgetItem *item = currentItem();
    if (!item) return;
    QString key = item->data(0, KEY).toString();
    if (!key.startsWith("host:") && !key.startsWith("folder:")) return;
    auto *mime = new QMimeData;
    mime->setData(MIME, key.toUtf8());
    auto *drag = new QDrag(this);
    drag->setMimeData(mime);
    QPixmap pm(Icons::pixmap(key.startsWith("host:") ? "server" : "folder", 16, Theme::text, 2, devicePixelRatioF()));
    drag->setPixmap(pm);
    dragged = key;
    drag->exec(Qt::MoveAction);
    dragged.clear();
    target = QModelIndex();
    viewport()->update();
}

void HostTree::dragEnterEvent(QDragEnterEvent *e) {
    if (e->mimeData()->hasFormat(MIME)) e->acceptProposedAction();
    else e->ignore();
}

void HostTree::dragMoveEvent(QDragMoveEvent *e) {
    QString d = QString::fromUtf8(e->mimeData()->data(MIME));
    QTreeWidgetItem *over = itemAt(e->position().toPoint());
    bool ok = accept(d, destination(over));
    QPersistentModelIndex t = ok ? QPersistentModelIndex(indexFromItem(over)) : QPersistentModelIndex();
    if (t != target) {
        target = t;
        viewport()->update();
    }
    if (ok) e->acceptProposedAction();
    else e->ignore();
}

void HostTree::dragLeaveEvent(QDragLeaveEvent *) {
    target = QModelIndex();
    viewport()->update();
}

void HostTree::dropEvent(QDropEvent *e) {
    QString d = QString::fromUtf8(e->mimeData()->data(MIME));
    QString dest = destination(itemAt(e->position().toPoint()));
    target = QModelIndex();
    viewport()->update();
    if (!accept(d, dest)) {
        e->ignore();
        return;
    }
    e->acceptProposedAction();
    // After the drop: a confirmation must not open during the drag.
    QMetaObject::invokeMethod(this, [this, d, dest] {
        if (d.startsWith("host:")) model->request("host.move", {{"host", d.mid(5)}, {"folder", dest}});
        else actions->moveFolderTo(d.mid(7), dest);
    }, Qt::QueuedConnection);
}
