// The tabs and splits of the Model, against tests/fakecore.py.
#include "app/core/Backend.h"
#include "app/core/Model.h"
#include "terminal/TerminalView.h"

#include <QJsonArray>
#include <QtTest>

class ModelTest : public QObject {
    Q_OBJECT

    Backend *backend = nullptr;
    Model *model = nullptr;

    /** Waits for a reply of the fake backend. */
    QJsonValue ask(const QString &op) {
        QJsonValue got;
        bool done = false;
        backend->request(op, {}, [&](const QJsonValue &v, const QString &) {
            got = v;
            done = true;
        });
        if (!QTest::qWaitFor([&] { return done; }, 5000)) qFatal("no reply to %s", qPrintable(op));
        return got;
    }

    static bool connected(Session *s) { return s->state == Session::State::Connected; }

    static QString name(Session::State s) {
        using State = Session::State;
        return s == State::Connected ? "CONNECTED" : s == State::Error ? "ERROR" : s == State::Closed ? "CLOSED" : "CONNECTING";
    }

    /** The parts of the tab as "host:state", left to right. */
    static QStringList parts(Session *tab) {
        QStringList r;
        for (Session *p : tab->panes()) r << p->hostId + ":" + name(p->state);
        return r;
    }

    QStringList tabHosts() const {
        QStringList r;
        for (Session *t : model->tabs()) r << t->hostId;
        return r;
    }

    /** What the shell of `s` would get if the user typed `text`. */
    void type(Session *s, const QString &text) { backend->request("session.input", {{"session", s->backend}, {"data", text}}); }

    /** The sessions the fake backend got some input for. */
    QStringList inputsTo() {
        QStringList r;
        for (const QJsonValue &v : ask("fake.log").toArray())
            if (v["op"] == "session.input") r << v["session"].toString();
        return r;
    }

private slots:
    void init() {
        qputenv("LEMURI_CORE", QFINDTESTDATA("fakecore.py").toUtf8());
        backend = new Backend;
        backend->start();
        model = new Model(backend);
        QJsonObject hosts = ask("hosts").toObject();
        emit backend->event("hosts", hosts);
        QCOMPARE(model->hosts().size(), 2);
    }

    void cleanup() {
        delete model;
        delete backend;
        QTest::qWait(10);  // the terminals go on the next turn
    }

    void anAnswerForAWidgetThatIsGoneIsDropped() {
        // A host's page asks for its updates, and is left before the server answers: the answer must not reach it.
        auto *page = new QObject;
        int calls = 0;
        model->request(page, "anything", {}, [&calls](const QJsonValue &) { calls++; });
        delete page;
        auto *kept = new QObject;
        model->request(kept, "anything", {}, [&calls](const QJsonValue &) { calls += 10; });
        ask("fake.log");  // answered after both
        QCOMPARE(calls, 10);
        delete kept;
    }

    void aTabOpensAndConnects() {
        Session *s = model->openSession("h1");
        QVERIFY(s);
        QTRY_VERIFY(connected(s));
        QCOMPARE(s->title, QString("alpha #1"));
        QCOMPARE(model->active().value("h1"), s);
        QCOMPARE(model->position().key, QString("h1"));
        QCOMPARE(model->position().item, QString("console"));
    }

    void splitPartsStayAfterTheirTab() {
        Session *a = model->openSession("h1");
        Session *b = model->openSession("h2");
        Session *a2 = model->split(a, true);
        Session *a3 = model->split(a2, false, "h2");
        QTRY_VERIFY(connected(a2) && connected(a3));
        QCOMPARE(model->sessions(), (QList<Session *>{a, a2, a3, b}));
        QCOMPARE(model->tabs(), (QList<Session *>{a, b}));
        QCOMPARE(a3->owner(), a);
        QVERIFY(a->layout()->divided());
        QVERIFY(a->layout()->sideBySide());
        QCOMPARE(parts(a), (QStringList{"h1:CONNECTED", "h1:CONNECTED", "h2:CONNECTED"}));
    }

    /** The shape of a layout: "h1" for a terminal, "(a|b)" side by side, "(a/b)" one above the other. */
    static QString shape(Pane *p) {
        if (!p->divided()) return p->session()->hostId;
        return "(" + shape(p->first()) + (p->sideBySide() ? "|" : "/") + shape(p->second()) + ")";
    }

    void serversOpenedTogetherFillTabsOfFourAsAGrid() {
        // Seven names, one unknown: four in a grid, then two side by side; the first tab in view.
        model->openSplit({"h1", "h2", "h1", "h2", "nope", "h2", "h1"});
        QList<Session *> tabs = model->tabs();
        QCOMPARE(tabs.size(), 2);
        QCOMPARE(shape(tabs[0]->layout()), QString("((h1/h1)|(h2/h2))"));
        QCOMPARE(shape(tabs[1]->layout()), QString("(h2|h1)"));
        QCOMPARE(model->position().key, QString("h1"));
        // Three: the first on the left, the others one above the other on the right.
        model->openSplit({"h2", "h1", "h2"});
        QCOMPARE(shape(model->tabs().last()->layout()), QString("(h2|(h1/h2))"));
        // One alone: a tab without splits.
        model->openSplit({"h1"});
        QVERIFY(!model->tabs().last()->layout());
    }

    void exitInAPartLeavesALocalShellInItsPlace() {
        Session *a = model->openSession("h1");
        Session *a2 = model->split(a, true);
        Session *a3 = model->split(a2, true, "h2");
        QTRY_VERIFY(connected(a2) && connected(a3));
        type(a2, "exit\r");
        QTRY_COMPARE(parts(a), (QStringList{"h1:CONNECTED", "local:CONNECTED", "h2:CONNECTED"}));
        QVERIFY(!model->sessions().contains(a2));
        QCOMPARE(model->tabs(), QList<Session *>{a});
    }

    void exitInATabLeavesALocalTabInItsPlace() {
        Session *a = model->openSession("h1");
        Session *b = model->openSession("h2");
        QTRY_VERIFY(connected(a) && connected(b));
        type(a, "exit\r");
        QTRY_COMPARE(tabHosts(), (QStringList{"local", "h2"}));
        QVERIFY(!model->sessions().contains(a));
        QVERIFY(!model->active().contains("h1"));
    }

    void exitInALocalTabClosesIt() {
        Session *l = model->openLocal();
        Session *a = model->openSession("h1");
        QTRY_VERIFY(connected(l) && connected(a));
        type(l, "exit\r");
        QTRY_COMPARE(model->tabs(), QList<Session *>{a});
    }

    void closingTheFirstPartPassesTheTabOn() {
        Session *a = model->openSession("h1");
        Session *b = model->openSession("h2");
        Session *a2 = model->split(a, true, "h2");
        Session *a3 = model->split(a2, false);
        QTRY_VERIFY(connected(a3));
        model->setSynced(a, true);
        model->closePane(a);
        // The heir is the first of the other parts, in the tab's place in the row, and keeps the rest.
        QCOMPARE(model->tabs(), (QList<Session *>{a2, b}));
        QCOMPARE(a2->owner(), nullptr);
        QCOMPARE(a3->owner(), a2);
        QCOMPARE(a2->panes(), (QList<Session *>{a2, a3}));
        QVERIFY(a2->synced);
        QVERIFY(!model->active().contains("h1"));
        QCOMPARE(model->active().value("h2"), b);
    }

    void closingTheLastButOnePartUnsyncs() {
        Session *a = model->openSession("h1");
        Session *a2 = model->split(a, true);
        QTRY_VERIFY(connected(a2));
        model->setSynced(a, true);
        model->closePane(a2);
        QVERIFY(!a->synced);
        QVERIFY(!a->layout());
        QCOMPARE(a->panes(), QList<Session *>{a});
    }

    void closingATabClosesAllItsParts() {
        Session *a = model->openSession("h1");
        Session *a2 = model->split(a, true, "h2");
        Session *b = model->openSession("h2");
        QTRY_VERIFY(connected(a2) && connected(b));
        QString first = a->backend, second = a2->backend;
        model->closeSession(a2);
        QCOMPARE(model->sessions(), QList<Session *>{b});
        QCOMPARE(model->active().value("h2"), b);
        QStringList closed;
        for (const QJsonValue &v : ask("fake.log").toArray())
            if (v["op"] == "session.close") closed << v["session"].toString();
        QCOMPARE(closed, (QStringList{first, second}));
    }

    void syncedTabsTypeInEveryPart() {
        Session *a = model->openSession("h1");
        Session *a2 = model->split(a, true);
        QTRY_VERIFY(connected(a) && connected(a2));
        emit a2->view->input("x");
        QTRY_COMPARE(inputsTo(), QStringList{a2->backend});
        model->setSynced(a, true);
        emit a2->view->input("y");
        QTRY_COMPARE(inputsTo(), (QStringList{a2->backend, a->backend, a2->backend}));
    }

    void showOrOpenReusesALiveTab() {
        Session *a = model->openSession("h1");
        QTRY_VERIFY(connected(a));
        model->openSession("h2");
        QCOMPARE(model->showOrOpen("h1"), a);
        QCOMPARE(model->tabs().size(), 2);
        model->disconnectHost("h1");
        QTRY_VERIFY(a->state == Session::State::Closed);
        // Disconnected by hand: no local shell takes its place, and the next click opens a new one.
        QCOMPARE(tabHosts(), (QStringList{"h1", "h2"}));
        Session *again = model->showOrOpen("h1");
        QVERIFY(again != a);
        QCOMPARE(model->tabs().size(), 3);
    }

    void aRetryTakesThePlaceOfTheFailedPart() {
        Session *a = model->openSession("h1");
        Session *a2 = model->split(a, true, "h2");
        QTRY_VERIFY(connected(a2));
        Session *fresh = model->retry(a2);
        QVERIFY(fresh && fresh != a2);
        QCOMPARE(a->panes(), (QList<Session *>{a, fresh}));
        QCOMPARE(fresh->hostId, QString("h2"));
    }

    // ── The backend gone ──

    void aRequestWaitingWhenTheBackendEndsGetsAnError() {
        qputenv("LEMURI_CORE", "/bin/true");
        Backend gone;
        gone.start();
        QString error = "none";
        gone.request("hosts", {}, [&](const QJsonValue &, const QString &e) { error = e; });
        // "exit code 0", or the write error that can come first when the process is already gone (under ASan, often).
        QTRY_VERIFY(error != "none" && !error.isEmpty());
        // After it has gone: an error too, never a request left waiting.
        QString later = "none";
        gone.request("hosts", {}, [&](const QJsonValue &, const QString &e) { later = e; });
        QTRY_VERIFY(later != "none" && !later.isEmpty());
    }

    void aBackendThatCannotStartFailsItsRequests() {
        qputenv("LEMURI_CORE", "/nonexistent/lemuri-core");
        Backend missing;
        QSignalSpy failed(&missing, &Backend::failed);
        missing.start();
        QString error = "none";
        missing.request("hosts", {}, [&](const QJsonValue &, const QString &e) { error = e; });
        QTRY_VERIFY(error != "none" && !error.isEmpty());
        QCOMPARE(failed.size(), 1);
    }
};

QTEST_MAIN(ModelTest)
#include "ModelTest.moc"
