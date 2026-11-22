// MessageFormat patterns ({0}, '' for ') as the messages*.properties files have them.
#include "app/tools/Texts.h"

#include <QtTest>

class TextsTest : public QObject {
    Q_OBJECT
private slots:
    void initTestCase() {
        Texts::load({{"plain", "Hosts"}, {"values", "{0} of {1}"}, {"apostrophe", "L''host {0}"}, {"quoted", "'{0}' is {0}"},
                     {"typed", "{0,number,integer} items"}, {"reversed", "{1} {0} {2}"}},
                "it");
    }
    void formats() {
        QCOMPARE(T("plain"), QString("Hosts"));
        QCOMPARE(T("values", 3, "10"), QString("3 of 10"));
        QCOMPARE(T("apostrophe", "alpha"), QString("L'host alpha"));
        QCOMPARE(T("quoted", "x"), QString("{0} is x"));
        QCOMPARE(T("typed", 7), QString("7 items"));
        QCOMPARE(T("reversed", "a", "b"), QString("b a {2}"));
    }
    void missingKeysShow() {
        QCOMPARE(T("nope"), QString("!nope!"));
    }
};

QTEST_MAIN(TextsTest)
#include "TextsTest.moc"
