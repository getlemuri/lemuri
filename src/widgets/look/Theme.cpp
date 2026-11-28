#include "widgets/look/Theme.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPalette>
#include <QPointer>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QWidget>

#include <vector>

namespace Theme {
namespace {
/** Every colour by its name: what a theme replaces, what a style sheet's @name finds. */
const QHash<QString, QColor *> &all() {
    static const QHash<QString, QColor *> named{
        {"backdropFrom", &backdropFrom},
        {"backdropMiddle", &backdropMiddle},
        {"backdropTo", &backdropTo},
        {"backdropWave", &backdropWave},
        {"backdropCrest", &backdropCrest},
        {"veil", &veil},
        {"veilHead", &veilHead},
        {"veilDeep", &veilDeep},
        {"track", &track},
        {"hover", &hover},
        {"hoverSoft", &hoverSoft},
        {"bg", &bg},
        {"surface", &surface},
        {"headPanel", &headPanel},
        {"side", &side},
        {"control", &control},
        {"checkEdge", &checkEdge},
        {"checkEdgeHover", &checkEdgeHover},
        {"controlHover", &controlHover},
        {"border", &border},
        {"edge", &edge},
        {"on", &on},
        {"gap", &gap},
        {"black", &black},
        {"tip", &tip},
        {"tipText", &tipText},
        {"text", &text},
        {"faint", &faint},
        {"disabled", &disabled},
        {"topText", &topText},
        {"topFaint", &topFaint},
        {"accent", &accent},
        {"accentDark", &accentDark},
        {"accentOff", &accentOff},
        {"accentOffText", &accentOffText},
        {"textSelection", &textSelection},
        {"rowSelected", &rowSelected},
        {"menuSelected", &menuSelected},
        {"listSelected", &listSelected},
        {"badge", &badge},
        {"activityBg", &activityBg},
        {"activityIcon", &activityIcon},
        {"activityActive", &activityActive},
        {"statusBlue", &statusBlue},
        {"treeOn", &treeOn},
        {"treeChoice", &treeChoice},
        {"treeFocusChoice", &treeFocusChoice},
        {"chevron", &chevron},
        {"dropTarget", &dropTarget},
        {"dropText", &dropText},
        {"sessionsIcon", &sessionsIcon},
        {"teamIcon", &teamIcon},
        {"updateIcon", &updateIcon},
        {"vaultIcon", &vaultIcon},
        {"ok", &ok},
        {"idle", &idle},
        {"critical", &critical},
        {"dangerText", &dangerText},
        {"criticalFill", &criticalFill},
        {"criticalFillHover", &criticalFillHover},
        {"danger", &danger},
        {"warn", &warn},
        {"amber", &amber},
        {"failed", &failed},
        {"failure", &failure},
        {"errorSoft", &errorSoft},
        {"okBack", &okBack},
        {"okText", &okText},
        {"warnBack", &warnBack},
        {"errorBack", &errorBack},
        {"errorText", &errorText},
        {"infoBack", &infoBack},
        {"infoText", &infoText},
        {"terminalActions", &terminalActions},
        {"terminalBg", &terminalBg},
        {"terminalFg", &terminalFg},
        {"terminalSelection", &terminalSelection},
        {"actionIcon", &actionIcon},
        {"actionHover", &actionHover},
        {"sync", &sync},
        {"syncedBorder", &syncedBorder},
        {"ghostControl", &ghostControl},
        {"ghostAccent", &ghostAccent},
        {"handle", &handle},
        {"handleTop", &handleTop},
        {"handleBottom", &handleBottom},
        {"handleLitTop", &handleLitTop},
        {"handleLitBottom", &handleLitBottom},
        {"handleEdge", &handleEdge},
        {"handleEdgeLit", &handleEdgeLit}};
    return named;
}

struct Palette {
    const char *id;
    bool light;
    /** The colours that differ from the first theme's; the others stay. */
    QHash<QString, QColor> colours;
    /** The terminal's 16 ANSI colours, if not the first theme's. */
    QList<QColor> terminal;
};

const QList<Palette> &palettes() {
    static const QList<Palette> p{
        // Lemuri: the colours of Theme.h.
        {"lemuri", false, {}, {}},
        // Night blue: the same build on a deep navy, with a brighter blue.
        {"night", false, {
            {"backdropFrom", QColor("#0a0f1c")},
            {"backdropMiddle", QColor("#0f1729")},
            {"backdropTo", QColor("#172544")},
            {"backdropWave", QColor("#3b5b9a")},
            {"backdropCrest", QColor("#8fb0ef")},
            {"bg", QColor("#141b2d")},
            {"surface", QColor("#141b2d")},
            {"headPanel", QColor("#1a2238")},
            {"side", QColor("#1a2238")},
            {"control", QColor("#1b2440")},
            {"controlHover", QColor("#243052")},
            {"checkEdge", QColor("#6f7b94")},
            {"checkEdgeHover", QColor("#a3afc6")},
            {"border", QColor("#232d47")},
            {"edge", QColor("#2e3a5a")},
            {"on", QColor("#1f2a44")},
            {"gap", QColor("#2a3550")},
            {"tip", QColor("#1e2740")},
            {"tipText", QColor("#d3dbea")},
            {"text", QColor("#e4e9f2")},
            {"faint", QColor("#9aa7bd")},
            {"disabled", QColor("#5f6b82")},
            {"topText", QColor("#e1e7f0")},
            {"topFaint", QColor("#c3cddd")},
            {"accent", QColor("#4f8ff7")},
            {"accentDark", QColor("#3a78e0")},
            {"accentOff", QColor("#263f6e")},
            {"accentOffText", QColor("#a8b8d8")},
            {"textSelection", QColor("#27467d")},
            {"rowSelected", QColor("#26314f")},
            {"menuSelected", QColor("#2b3a5e")},
            {"listSelected", QColor("#1f3f73")},
            {"badge", QColor("#3b7cf0")},
            {"activityBg", QColor("#161e33")},
            {"activityIcon", QColor("#8593ad")},
            {"statusBlue", QColor("#3a78e0")},
            {"treeOn", QColor("#1f2a44")},
            {"treeChoice", QColor("#24365e")},
            {"treeFocusChoice", QColor("#2b4170")},
            {"chevron", QColor("#8593ad")},
            {"infoBack", QColor("#17345e")},
            {"terminalActions", QColor("#0b1120")},
            {"terminalBg", QColor("#0b1120")},
            {"terminalFg", QColor("#d6deeb")},
            {"terminalSelection", QColor("#27467d")},
            {"handle", QColor("#1f2942")},
            {"handleTop", QColor("#253050")},
            {"handleBottom", QColor("#1c2540")},
            {"handleLitTop", QColor("#2f3b60")},
            {"handleLitBottom", QColor("#263152")},
            {"handleEdge", QColor("#34405f")},
            {"handleEdgeLit", QColor("#4d5a7d")}}, {}},
        // Graphite: neutral greys a little lighter and softer than Lemuri's, with a violet accent.
        {"graphite", false, {
            {"backdropFrom", QColor("#121212")},
            {"backdropMiddle", QColor("#1a1919")},
            {"backdropTo", QColor("#2a2730")},
            {"backdropWave", QColor("#6e6380")},
            {"backdropCrest", QColor("#c3b6d9")},
            {"bg", QColor("#232323")},
            {"surface", QColor("#232323")},
            {"headPanel", QColor("#2a2a2a")},
            {"side", QColor("#2a2a2a")},
            {"control", QColor("#2c2c2c")},
            {"controlHover", QColor("#363636")},
            {"border", QColor("#333333")},
            {"edge", QColor("#414141")},
            {"on", QColor("#303030")},
            {"gap", QColor("#383838")},
            {"tip", QColor("#303030")},
            {"tipText", QColor("#d6d6d6")},
            {"text", QColor("#dcdcdc")},
            {"faint", QColor("#a3a3a3")},
            {"disabled", QColor("#707070")},
            {"topText", QColor("#dadada")},
            {"topFaint", QColor("#c4c4c4")},
            {"accent", QColor("#8b6fe0")},
            {"accentDark", QColor("#7558cc")},
            {"accentOff", QColor("#463a6b")},
            {"accentOffText", QColor("#bdb3d6")},
            {"textSelection", QColor("#4a3d73")},
            {"rowSelected", QColor("#3a3840")},
            {"menuSelected", QColor("#403c4a")},
            {"listSelected", QColor("#43386b")},
            {"badge", QColor("#7c5fe0")},
            {"statusBlue", QColor("#7558cc")},
            {"treeOn", QColor("#2f2f33")},
            {"treeChoice", QColor("#37324a")},
            {"treeFocusChoice", QColor("#43395f")},
            {"dropTarget", QColor("#9b82f0")},
            {"dropText", QColor("#cbbdf7")},
            {"ghostAccent", QColor("#b9a6ff")},
            {"infoBack", QColor("#2f2a4d")},
            {"infoText", QColor("#bfb0f0")},
            {"terminalActions", QColor("#161616")},
            {"terminalBg", QColor("#1b1b1b")},
            {"terminalFg", QColor("#dcdcdc")},
            {"terminalSelection", QColor("#4a3d73")}}, {}},
        // Light: white surfaces on a pale backdrop; every text and state colour darkened to read on them (4.5:1),
        // and a terminal on white, whose ANSI colours are the darker ones (yellow on white is not readable).
        {"light", true, {
            {"backdropFrom", QColor("#f7f9fb")},
            {"backdropMiddle", QColor("#eef2f5")},
            {"backdropTo", QColor("#dde7ee")},
            {"backdropWave", QColor("#8fb0c4")},
            {"backdropCrest", QColor("#5f88a3")},
            {"veil", QColor(255,255,255,150)},
            {"veilHead", QColor(255,255,255,200)},
            {"veilDeep", QColor(255,255,255,225)},
            {"track", QColor(0,0,0,26)},
            {"hover", QColor(0,0,0,20)},
            {"hoverSoft", QColor(0,0,0,14)},
            {"bg", QColor("#ffffff")},
            {"surface", QColor("#ffffff")},
            {"headPanel", QColor("#f3f5f7")},
            {"side", QColor("#f3f5f7")},
            {"control", QColor("#ffffff")},
            {"controlHover", QColor("#eef1f4")},
            {"checkEdge", QColor("#8a939c")},
            {"checkEdgeHover", QColor("#5c6670")},
            {"border", QColor("#d9dee3")},
            {"edge", QColor("#c4cbd2")},
            {"on", QColor("#e9edf1")},
            {"gap", QColor("#d5dbe0")},
            {"tip", QColor("#2b2f33")},
            {"tipText", QColor("#f0f2f4")},
            {"text", QColor("#1f2328")},
            {"faint", QColor("#56606a")},
            {"disabled", QColor("#9aa3ab")},
            {"topText", QColor("#1f2328")},
            {"topFaint", QColor("#4b555f")},
            {"accent", QColor("#1f6fc5")},
            {"accentDark", QColor("#185ea9")},
            {"accentOff", QColor("#cfe0f3")},
            {"accentOffText", QColor("#2f5680")},
            {"textSelection", QColor("#b6d4f5")},
            {"rowSelected", QColor("#e3e8ee")},
            {"menuSelected", QColor("#dde5ee")},
            {"listSelected", QColor("#cfe1f6")},
            {"badge", QColor("#1f6fc5")},
            {"activityBg", QColor("#e9edf1")},
            {"activityIcon", QColor("#6b7580")},
            {"activityActive", QColor("#1f2328")},
            {"statusBlue", QColor("#1f6fc5")},
            {"treeOn", QColor("#e6ebf0")},
            {"treeChoice", QColor("#d6e4f5")},
            {"treeFocusChoice", QColor("#c4d9f2")},
            {"chevron", QColor("#6b7580")},
            {"dropTarget", QColor("#1f6fc5")},
            {"dropText", QColor("#1f5aa8")},
            {"sessionsIcon", QColor("#0f7fc0")},
            {"teamIcon", QColor("#9b3f97")},
            {"updateIcon", QColor("#2e8a35")},
            {"vaultIcon", QColor("#a8741c")},
            {"ok", QColor("#1a9e3c")},
            {"idle", QColor("#8a9299")},
            {"critical", QColor("#d0293a")},
            {"dangerText", QColor("#c42131")},
            {"criticalFill", QColor("#c42131")},
            {"criticalFillHover", QColor("#a51b29")},
            {"danger", QColor("#c0473b")},
            {"warn", QColor("#9a6700")},
            {"amber", QColor("#c77c00")},
            {"failed", QColor("#c4472c")},
            {"failure", QColor("#cf222e")},
            {"errorSoft", QColor("#b4434a")},
            {"okBack", QColor("#dcf3e2")},
            {"okText", QColor("#1a7f37")},
            {"warnBack", QColor("#fbefd0")},
            {"errorBack", QColor("#fbe1e3")},
            {"errorText", QColor("#b42232")},
            {"infoBack", QColor("#dbeaf8")},
            {"infoText", QColor("#1f5a96")},
            {"terminalActions", QColor("#eef1f4")},
            {"terminalBg", QColor("#fbfcfd")},
            {"terminalFg", QColor("#24292f")},
            {"terminalSelection", QColor("#b6d4f5")},
            {"actionIcon", QColor("#5c6670")},
            {"actionHover", QColor("#1f2328")},
            {"sync", QColor("#b58900")},
            {"syncedBorder", QColor("#c9861a")},
            {"ghostControl", QColor("#5c6670")},
            {"ghostAccent", QColor("#1f6fc5")},
            {"handle", QColor("#d0d5da")},
            {"handleTop", QColor("#d6dbe0")},
            {"handleBottom", QColor("#c9cfd5")},
            {"handleLitTop", QColor("#bcc3ca")},
            {"handleLitBottom", QColor("#b2bac2")},
            {"handleEdge", QColor("#b8c0c8")},
            {"handleEdgeLit", QColor("#9aa3ac")}},
         {QColor("#2e3436"), QColor("#c01c28"), QColor("#2f7a0e"), QColor("#8a6d00"), QColor("#1f5fae"), QColor("#8b3d8f"), QColor("#0b7a7c"), QColor("#6e737a"), QColor("#555753"), QColor("#d42a2a"), QColor("#3d8f12"), QColor("#9e7c00"), QColor("#2a6cc2"), QColor("#a24ea6"), QColor("#0e8f91"), QColor("#2e3436")}},
    };
    return p;
}

QString active = "lemuri";
bool isLight = false;

/** The property where style() keeps a widget's sheet with its @names. */
constexpr const char *SHEET = "lemuriThemeSheet";
QString appSheet;
std::vector<std::pair<QPointer<QObject>, std::function<void()>>> followers;

/** The first theme's colours, as the app started with them: a change starts again from these. */
struct First {
    QHash<QString, QColor> colours;
    QList<QColor> terminal;
};
const First &first() {
    static const First kept = [] {
        First f;
        for (auto it = all().begin(); it != all().end(); ++it) f.colours.insert(it.key(), *it.value());
        for (const QColor &c : terminalPalette) f.terminal << c;
        return f;
    }();
    return kept;
}

QString startupFile() {
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/startup.json";
}
}  // namespace

const QList<Choice> &choices() {
    static const QList<Choice> c{{"lemuri", "theme.lemuri"}, {"night", "theme.night"}, {"graphite", "theme.graphite"}, {"light", "theme.light"}};
    return c;
}

void apply(const QString &id) {
    // Back to the first theme, then the differences of this one.
    const First &f = first();
    for (auto it = f.colours.begin(); it != f.colours.end(); ++it) *all().value(it.key()) = it.value();
    for (int i = 0; i < 16; i++) terminalPalette[i] = f.terminal[i];
    active = "lemuri";
    isLight = false;
    for (const Palette &p : palettes()) {
        if (id != QLatin1String(p.id)) continue;
        for (auto it = p.colours.begin(); it != p.colours.end(); ++it) *all().value(it.key()) = it.value();
        for (int i = 0; i < p.terminal.size() && i < 16; i++) terminalPalette[i] = p.terminal[i];
        active = p.id;
        isLight = p.light;
        return;
    }
}

void change(const QString &id) {
    if (id == active) return;
    apply(id);
    qApp->setPalette(palette());
    if (!appSheet.isEmpty()) qApp->setStyleSheet(css(appSheet));
    const QList<QWidget *> widgets = QApplication::allWidgets();
    for (QWidget *w : widgets) {
        QVariant sheet = w->property(SHEET);
        if (sheet.isValid()) w->setStyleSheet(css(sheet.toString()));
    }
    // A follower may make widgets or followers of its own: the list as it is now.
    auto now = followers;
    for (auto &[context, f] : now)
        if (context) f();
    for (QWidget *w : QApplication::allWidgets()) w->update();
}

QPalette palette() {
    QPalette p;
    p.setColor(QPalette::Window, headPanel);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, bg);
    p.setColor(QPalette::AlternateBase, control);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, border);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, accent);
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::ToolTipBase, headPanel);
    p.setColor(QPalette::ToolTipText, text);
    p.setColor(QPalette::PlaceholderText, faint);
    p.setColor(QPalette::Disabled, QPalette::Text, disabled);
    p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    return p;
}

void style(QWidget *w, const QString &sheet) {
    if (!w) {
        appSheet = sheet;
        qApp->setStyleSheet(css(sheet));
        return;
    }
    w->setProperty(SHEET, sheet);
    w->setStyleSheet(css(sheet));
}

void follow(QObject *context, std::function<void()> f) {
    std::erase_if(followers, [](const auto &e) { return !e.first; });
    followers.emplace_back(context, std::move(f));
}

bool named(const QColor *c) {
    static const QSet<const QColor *> colours = [] {
        QSet<const QColor *> s;
        for (QColor *one : all()) s.insert(one);
        for (const QColor &one : terminalPalette) s.insert(&one);
        return s;
    }();
    return colours.contains(c);
}

QString current() { return active; }

bool light() { return isLight; }

QString saved() {
    QFile f(startupFile());
    if (!f.open(QIODevice::ReadOnly)) return "lemuri";
    return QJsonDocument::fromJson(f.readAll()).object()["theme"].toString("lemuri");
}

void remember(const QString &id) {
    QFile f(startupFile());
    QJsonObject o;
    if (f.open(QIODevice::ReadOnly)) o = QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    if (o["theme"].toString() == id) return;
    o["theme"] = id;
    QDir().mkpath(QFileInfo(startupFile()).path());
    if (f.open(QIODevice::WriteOnly)) f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
}

QString css(QString sheet) {
    static const QRegularExpression token("@([A-Za-z]+)");
    QString out;
    qsizetype last = 0;
    for (auto it = token.globalMatch(sheet); it.hasNext();) {
        QRegularExpressionMatch m = it.next();
        QColor *c = all().value(m.captured(1));
        out += sheet.mid(last, m.capturedStart() - last);
        if (!c) out += m.captured(0);
        else if (c->alpha() < 255) out += QString("rgba(%1,%2,%3,%4)").arg(c->red()).arg(c->green()).arg(c->blue()).arg(c->alpha());
        else out += c->name();
        last = m.capturedEnd();
    }
    return out + sheet.mid(last);
}
}  // namespace Theme
