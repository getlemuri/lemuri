// Lemuri in the GNOME dock, against a fake gsettings that keeps the favourites in a file.
#include "app/Dock.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

class DockTest : public QObject {
    Q_OBJECT

    QTemporaryDir dir;

    QString saved() const {
        QFile f(dir.filePath("favourites"));
        if (!f.open(QIODevice::ReadOnly)) return {};
        return QString::fromUtf8(f.readAll()).trimmed();
    }

    void keep(const QString &value) {
        QFile f(dir.filePath("favourites"));
        QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Truncate));
        f.write(value.toUtf8() + "\n");
    }

private slots:
    void initTestCase() {
        QVERIFY(dir.isValid());
        // gsettings get/set org.gnome.shell favorite-apps, on a file; anything else fails as gsettings does.
        QFile tool(dir.filePath("bin/gsettings"));
        QVERIFY(QDir().mkpath(dir.filePath("bin")));
        QVERIFY(tool.open(QIODevice::WriteOnly));
        tool.write("#!/bin/sh\n"
                   "f=\"$(dirname \"$0\")/../favourites\"\n"
                   "[ \"$2 $3\" = 'org.gnome.shell favorite-apps' ] || exit 1\n"
                   "case $1 in get) cat \"$f\" ;; set) printf '%s\\n' \"$4\" > \"$f\" ;; *) exit 1 ;; esac\n");
        tool.close();
        tool.setPermissions(QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
        QVERIFY(QDir().mkpath(dir.filePath("share/applications")));
        QFile entry(dir.filePath("share/applications/lemuri.desktop"));
        QVERIFY(entry.open(QIODevice::WriteOnly));
        entry.write("[Desktop Entry]\nType=Application\nName=Lemuri\nExec=lemuri\n");
        entry.close();
        qputenv("PATH", dir.filePath("bin").toUtf8() + ":" + qgetenv("PATH"));
        // Only this folder of applications: not the system's, where Lemuri may be installed.
        qputenv("XDG_DATA_HOME", dir.filePath("share").toUtf8());
        qputenv("XDG_DATA_DIRS", dir.filePath("none").toUtf8());
        qputenv("XDG_CURRENT_DESKTOP", "ubuntu:GNOME");
    }

    void theFavouritesAreReadAsGsettingsPrintsThem() {
        QCOMPARE(Dock::parse("['firefox_firefox.desktop', 'org.gnome.Nautilus.desktop']")
                     .value_or(QStringList{"unreadable"}),
                 QStringList({"firefox_firefox.desktop", "org.gnome.Nautilus.desktop"}));
        QCOMPARE(Dock::parse("@as []").value_or(QStringList{"unreadable"}), QStringList());
        QCOMPARE(Dock::parse("[]").value_or(QStringList{"unreadable"}), QStringList());
        QVERIFY(!Dock::parse("No such key"));
        QCOMPARE(Dock::print({"a.desktop", "lemuri.desktop"}), QString("['a.desktop', 'lemuri.desktop']"));
    }

    void lemuriGoesAtTheEndAndComesAwayLeavingTheOthers() {
        keep("['firefox_firefox.desktop', 'terminator.desktop']");
        QVERIFY(Dock::available());
        QVERIFY(!Dock::pinned());
        QVERIFY(Dock::pin(true));
        QCOMPARE(saved(), QString("['firefox_firefox.desktop', 'terminator.desktop', 'lemuri.desktop']"));
        QVERIFY(Dock::pinned());
        // Twice: still once.
        QVERIFY(Dock::pin(true));
        QCOMPARE(saved(), QString("['firefox_firefox.desktop', 'terminator.desktop', 'lemuri.desktop']"));
        QVERIFY(Dock::pin(false));
        QCOMPARE(saved(), QString("['firefox_firefox.desktop', 'terminator.desktop']"));
        QVERIFY(!Dock::pinned());
    }

    void anEmptyDockGetsLemuri() {
        keep("@as []");
        QVERIFY(Dock::pin(true));
        QCOMPARE(saved(), QString("['lemuri.desktop']"));
    }

    void notOnOtherDesktops() {
        qputenv("XDG_CURRENT_DESKTOP", "KDE");
        QVERIFY(!Dock::available());
        qputenv("XDG_CURRENT_DESKTOP", "ubuntu:GNOME");
    }

    void notWithoutTheInstalledApp() {
        QFile::rename(dir.filePath("share/applications/lemuri.desktop"),
                      dir.filePath("share/applications/other.desktop"));
        QVERIFY(!Dock::available());
        QFile::rename(dir.filePath("share/applications/other.desktop"),
                      dir.filePath("share/applications/lemuri.desktop"));
    }
};

QTEST_GUILESS_MAIN(DockTest)
#include "DockTest.moc"
