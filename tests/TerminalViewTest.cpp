// The terminal's reading of the shell (prompt, ls output, names under the mouse) and its hidden command.
#include "terminal/TerminalView.h"

#include <QSignalSpy>
#include <QtTest>

class TerminalViewTest : public QObject {
    Q_OBJECT

    using Hit = TerminalView::NameHit;
    using Point = TerminalView::Point;

    /** A terminal with these lines on it (\n between them); the cursor stays at the end of the last one. */
    static TerminalView *with(const QStringList &lines) {
        auto *t = new TerminalView;
        // As a user who turned the setting on: the tests of what is typed need it.
        t->setTypesCommands(true);
        t->resize(800, 400);
        t->write(lines.join("\r\n"));
        return t;
    }

    static QString nameAt(TerminalView *t, int line, int col) { return t->nameAt(Point{line, col}).name; }

private slots:
    void promptsOfTheUsualShells() {
        for (const QString &prompt : {"anna@pc:~$ ", "root@srv:/etc# ", "[root@fedora ~]# ", "anna@Mac ~ % ", "u@h-1.lan:~/a b$ "}) {
            std::unique_ptr<TerminalView> t(with({prompt}));
            QVERIFY2(t->shellReady(), qPrintable(prompt));
        }
    }

    void textFillsItsCellsExactly() {
        TerminalView t;
        for (bool bold : {false, true}) {
            QFont f = t.font;
            f.setBold(bold);
            QCOMPARE(qRound(QFontMetricsF(f).horizontalAdvance(QString(80, 'a'))), 80 * t.cellW);
        }
    }

    void aColouredPromptIsStillAPrompt() {
        std::unique_ptr<TerminalView> t(with({"\x1b[01;32manna@pc\x1b[00m:\x1b[01;34m~\x1b[00m$ "}));
        QCOMPARE(t->lineText(0).trimmed(), QString("anna@pc:~$"));
        QVERIFY(t->shellReady());
    }

    void notReadyWithACommandTypedOrAFullScreenProgram() {
        std::unique_ptr<TerminalView> t(with({"anna@pc:~$ cat x"}));
        QVERIFY(!t->shellReady());
        std::unique_ptr<TerminalView> python(with({">>> "}));
        QVERIFY(!python->shellReady());
        std::unique_ptr<TerminalView> vim(with({"anna@pc:~$ "}));
        vim->write("\x1b[?1049h");
        QVERIFY(!vim->shellReady());
    }

    void namesOfAPlainLs() {
        std::unique_ptr<TerminalView> t(with({"anna@pc:~$ ls", "Documents  'my file'  notes.txt  bin/  run*", "anna@pc:~$ "}));
        QCOMPARE(nameAt(t.get(), 1, 0), QString("Documents"));
        QCOMPARE(nameAt(t.get(), 1, 8), QString("Documents"));
        QCOMPARE(nameAt(t.get(), 1, 14), QString("my file"));
        QCOMPARE(nameAt(t.get(), 1, 23), QString("notes.txt"));
        QCOMPARE(nameAt(t.get(), 1, 34), QString("bin"));
        QCOMPARE(nameAt(t.get(), 1, 40), QString("run"));
        QCOMPARE(nameAt(t.get(), 1, 10), QString());  // between two names
        QCOMPARE(t->nameAt(Point{1, 0}).command, QString("ls"));
        QVERIFY(t->shellReady(1));
    }

    void namesOfAnLsL() {
        std::unique_ptr<TerminalView> t(with({"root@srv:/# ls -la /", "total 64",
                "lrwxrwxrwx   1 root root     7 Apr 22  2024 bin -> usr/bin",
                "drwxr-xr-x  20 root root  4096 Nov 20 10:00 etc",
                "-rw-r--r--.  1 root root 1,234 Nov 20 10:00 a file.txt", "root@srv:/# "}));
        QCOMPARE(nameAt(t.get(), 1, 2), QString());
        QCOMPARE(nameAt(t.get(), 2, 45), QString("bin"));
        QCOMPARE(nameAt(t.get(), 2, 53), QString());  // the target of the link
        QCOMPARE(nameAt(t.get(), 3, 46), QString("etc"));
        QCOMPARE(nameAt(t.get(), 3, 0), QString());  // the permissions
        QCOMPARE(nameAt(t.get(), 4, 48), QString("a file.txt"));
        QCOMPARE(TerminalView::lsFolder(t->nameAt(Point{3, 46}).command), QString("/"));
    }

    void onlyTheOutputOfAnLs() {
        std::unique_ptr<TerminalView> t(with({"anna@pc:~$ cat list", "Documents notes.txt", "anna@pc:~$ cd x && ls", "a b", "anna@pc:~/x$ "}));
        QCOMPARE(nameAt(t.get(), 1, 0), QString());
        QCOMPARE(nameAt(t.get(), 3, 0), QString("a"));
        QCOMPARE(t->nameAt(Point{3, 0}).command, QString("cd x && ls"));
    }

    void anOlderLsCountsOnlyAtTheSamePrompt() {
        std::unique_ptr<TerminalView> t(with({"anna@pc:~$ ls", "a b", "anna@pc:~$ date", "Thu Nov 20", "anna@pc:~$ cd /tmp", "anna@pc:/tmp$ "}));
        QVERIFY(t->shellReady());
        QVERIFY(!t->shellReady(1));  // listed in ~, the shell is now in /tmp
        std::unique_ptr<TerminalView> same(with({"anna@pc:~$ ls", "a b", "anna@pc:~$ date", "Thu Nov 20", "anna@pc:~$ "}));
        QVERIFY(same->shellReady(1));
    }

    void theFolderAnLsListed() {
        QCOMPARE(TerminalView::lsFolder("ls"), QString());
        QCOMPARE(TerminalView::lsFolder("ls -la /etc"), QString("/etc"));
        QCOMPARE(TerminalView::lsFolder("cd /var && ls log"), QString("log"));
        QCOMPARE(TerminalView::lsFolder("ls 'my dir'"), QString("my dir"));
        QCOMPARE(TerminalView::lsFolder("ls a b"), QString());
    }

    void offUntilTheUserTurnsItOn() {
        TerminalView t;
        QVERIFY(!t.typesCommandsOn());
    }

    void theHiddenCommandReadsTheFolderAndLeavesNoTrace() {
        std::unique_ptr<TerminalView> t(with({"anna@pc:~$ "}));
        QSignalSpy typed(t.get(), &TerminalView::command);
        QString folder = "none";
        t->currentFolder([&](const QString &f) { folder = f; });
        QCOMPARE(typed.size(), 1);
        QVERIFY(typed[0][0].toByteArray().startsWith(" printf"));
        // The shell echoes the command, then prints the marks and a new prompt.
        t->write(" printf '\\033]777;lemuri\\007'; printf ...\r\n");
        t->write("\x1b]777;lemuri\x07\x1b]777;lemuri-pwd;/home/anna\x07");
        t->write("anna@pc:~$ ");
        QTRY_COMPARE(folder, QString("/home/anna"));
        QCOMPARE(t->lineText(0).trimmed(), QString("anna@pc:~$"));
        QCOMPARE(t->lineText(1).trimmed(), QString());
        QVERIFY(t->shellReady());
    }

    void theFolderAShellAnnouncesIsUsedWithoutTypingAnything() {
        QCOMPARE(TerminalView::announcedFolder("x\x1b]7;file://pc/home/anna/my%20dir\x07y"), QString("/home/anna/my dir"));
        QCOMPARE(TerminalView::announcedFolder("\x1b]7;file:///tmp\x1b\\"), QString("/tmp"));
        QVERIFY(TerminalView::announcedFolder("\x1b]7;file://pc/half").isNull());
        std::unique_ptr<TerminalView> t(with({}));
        t->setTypesCommands(false);
        QSignalSpy typed(t.get(), &TerminalView::command);
        // Cut in two, then the prompt.
        t->write("\x1b]7;file://pc/srv/");
        t->write("app\x07" "anna@pc:/srv/app$ ");
        QString folder = "none";
        t->currentFolder([&](const QString &f) { folder = f; });
        QTRY_COMPARE(folder, QString("/srv/app"));
        QCOMPARE(typed.size(), 0);
        // A command run (here: ssh elsewhere): not known any more until announced again.
        emit t->input("ssh other\r");
        t->write("\r\nroot@other:~# ");
        folder = "none";
        t->currentFolder([&](const QString &f) { folder = f; });
        QTRY_COMPARE(folder, QString());
    }

    void noHiddenCommandWhenTheShellIsBusyOrTheSettingIsOff() {
        std::unique_ptr<TerminalView> busy(with({"anna@pc:~$ top"}));
        QString folder = "none";
        busy->currentFolder([&](const QString &f) { folder = f; });
        QCOMPARE(folder, QString());
        std::unique_ptr<TerminalView> off(with({"anna@pc:~$ "}));
        off->setTypesCommands(false);
        QSignalSpy typed(off.get(), &TerminalView::command);
        QVERIFY(!off->typeCommand("ls"));
        off->setTypesCommands(true);
        QVERIFY(off->typeCommand("ls"));
        QCOMPARE(typed.size(), 1);
        QCOMPARE(typed[0][0].toByteArray(), QByteArray(" ls\r"));
    }

    void keysBecomeTheBytesOfATerminal() {
        std::unique_ptr<TerminalView> t(with({"anna@pc:~$ "}));
        QSignalSpy sent(t.get(), &TerminalView::input);
        QTest::keyClick(t.get(), Qt::Key_C, Qt::ControlModifier);
        QTest::keyClick(t.get(), Qt::Key_Up);
        QTest::keyClick(t.get(), Qt::Key_A);
        QTest::keyClick(t.get(), Qt::Key_Return);
        QByteArray all;
        for (const auto &s : sent) all += s[0].toByteArray();
        QCOMPARE(all, QByteArray("\x03\x1b[Aa\r"));
    }

    // Scrolled all the way back, then made taller: the screen takes lines back from the scrollback, and the view
    // must still start at a line that exists (it used to read before the first one and crash while painting).
    void aTallerTerminalScrolledBackStillPaints() {
        QStringList lines;
        for (int i = 0; i < 200; i++) lines << QString("line %1").arg(i);
        std::unique_ptr<TerminalView> t(with(lines));
        QVERIFY(t->scrollback.size() > 10);
        t->scrolled = int(t->scrollback.size());
        t->resize(800, 1600);
        QResizeEvent grow(t->size(), QSize(800, 400));
        QCoreApplication::sendEvent(t.get(), &grow);
        QVERIFY(t->firstShownLine() >= 0);
        QVERIFY(t->scrolled <= int(t->scrollback.size()));
        QVERIFY(!t->grab().isNull());
    }
};

QTEST_MAIN(TerminalViewTest)
#include "TerminalViewTest.moc"
