// What the package updater takes from GitHub's answer, and version order: the same cases as the first Lemuri's. The download and the signature are in tests/update_test.py.
#include "base/core/Error.h"
#include "updates/Updates.h"

#include <QtTest>

namespace {
const QByteArray ASSETS = "\"assets\":[{\"name\":\"lemuri_0.2.0_amd64.deb\",\"browser_download_url\":\"https://github.com/x/a.deb\"},\n"
    "                                      {\"name\":\"lemuri_0.2.0_amd64.deb.sig\",\"browser_download_url\":\"https://github.com/x/a.deb.sig\"}]";

std::optional<Release> parse(const QByteArray &json, const QString &arch = "amd64", bool anyUrl = false) {
    return PackageUpdater::parse(json, arch, anyUrl, nullptr);
}

bool fails(const QByteArray &json) {
    try {
        parse(json);
        return false;
    } catch (const Fail &) {
        return true;
    }
}
}  // namespace

class UpdatesTest : public QObject {
    Q_OBJECT
private slots:
    void releaseFromGitHub() {
        auto r = parse("{\"tag_name\":\"v0.2.0\",\"html_url\":\"https://github.com/getlemuri/lemuri/releases/tag/v0.2.0\",\"body\":\"### Added\\n- x\","
                       + ASSETS + "}");
        QVERIFY(r);
        QCOMPARE(r->tag, QString("v0.2.0"));
        QCOMPARE(r->version, QString("0.2.0"));
        QCOMPARE(r->notes, QString("### Added\n- x"));
        QCOMPARE(r->url, QString("https://github.com/getlemuri/lemuri/releases/tag/v0.2.0"));
    }

    void foreignPagesAreNotOpened() {
        auto r = parse("{\"tag_name\":\"v0.2.0\",\"html_url\":\"https://evil.example/x\"," + ASSETS + "}");
        QCOMPARE(r->url, QString("https://github.com/getlemuri/lemuri/releases/tag/v0.2.0"));
        QCOMPARE(r->notes, QString(""));
    }

    void oddTagsAreRefused() {
        QVERIFY(fails("{\"tag_name\":\"v0.2.0-rc1\"," + ASSETS + "}"));
        QVERIFY(fails("{\"tag_name\":\"latest\"}"));
        QVERIFY(fails("not json"));
    }

    void onlyReleasesWithThePackageForThisComputer() {
        QByteArray json = "{\"tag_name\":\"v0.2.0\"," + ASSETS + "}";
        QVERIFY(!parse(json, "arm64"));
        QVERIFY(!parse("{\"tag_name\":\"v0.2.0\",\"assets\":[]}"));
        QVERIFY(!parse("{\"tag_name\":\"v0.2.0\",\"assets\":[{\"name\":\"lemuri_0.2.0_amd64.deb\",\"browser_download_url\":\"https://x/a\"}]}"));
    }

    void downloadsOnlyOverHttps() {
        QByteArray plain = QByteArray(ASSETS).replace("https://", "http://");
        QVERIFY(!parse("{\"tag_name\":\"v0.2.0\"," + plain + "}"));
        QVERIFY(parse("{\"tag_name\":\"v0.2.0\"," + plain + "}", "amd64", true));
    }

    void versionOrder() {
        QVERIFY(Versions::compare("0.2.0", "0.1.9") > 0);
        QVERIFY(Versions::compare("0.10.0", "0.9.0") > 0);
        QVERIFY(Versions::compare("1.0", "0.99.99") > 0);
        QCOMPARE(Versions::compare("0.2", "0.2.0"), 0);
        QVERIFY(Versions::compare("0.1.0", "0.1.1") < 0);
        QVERIFY(Versions::valid("0.2.0"));
        QVERIFY(!Versions::valid("0.2.0-SNAPSHOT"));
        QVERIFY(!Versions::valid("0.2.0-rc1"));
        QVERIFY(!Versions::valid(""));
        QVERIFY(!Versions::valid(QString()));
    }
};

QTEST_GUILESS_MAIN(UpdatesTest)
#include "UpdatesTest.moc"
