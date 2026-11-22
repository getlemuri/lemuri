// The dialogs give their wrapped texts all the height they need, whatever the length and the language.
#include "widgets/parts/Ui.h"

#include <QLabel>
#include <QTimer>
#include <QVBoxLayout>
#include <QtTest>

class DialogTest : public QObject {
    Q_OBJECT

    /** The wrapped labels of the dialog open now that got less height than their text needs. */
    static QStringList cut() {
        QStringList bad;
        auto *d = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!d) return {"no dialog"};
        for (QLabel *l : d->findChildren<QLabel *>())
            if (l->wordWrap() && l->isVisible() && l->height() < l->heightForWidth(l->width()))
                bad << QString("%1 (%2 < %3)").arg(l->text()).arg(l->height()).arg(l->heightForWidth(l->width()));
        return bad;
    }

private slots:
    /** The app's font, as main() sets it. */
    void initTestCase() {
        QFont f("Helvetica Neue");
        f.setStyleHint(QFont::SansSerif);
        f.setPixelSize(12);
        QApplication::setFont(f);
    }

    void confirmAndWarningShowAllTheirText() {
        QStringList bad;
        QString name;
        for (int n = 1; n <= 120; n++) {
            name += QChar('a' + n % 26);
            if (n % 7 == 0) name += '-';
            QString text = n % 2 ? QString("Host %1 (192.0.2.12) will be removed from the list. Its notes will be lost.").arg(name)
                                 : QString("Folder %1 and its subfolders will be deleted. The host inside will move to Hosts.").arg(name.left(n / 4 + 1));
            for (int kind = 0; kind < 2; kind++) {
                QTimer::singleShot(0, [&] {
                    bad << cut();
                    QApplication::activeModalWidget()->close();
                });
                if (kind == 0) Ui::confirm(nullptr, "Remove host", text, "Remove", true);
                else Ui::warning(nullptr, "Remove host", text);
            }
        }
        QVERIFY2(bad.isEmpty(), qPrintable(bad.mid(0, 5).join("\n")));
    }
};

QTEST_MAIN(DialogTest)
#include "DialogTest.moc"
