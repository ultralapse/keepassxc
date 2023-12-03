/*
 *  Copyright (C) 2021 KeePassXC Team <team@keepassxc.org>
 *
 *  This program is free software: you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation, either version 2 or (at your option)
 *  version 3 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "TestBrowser.h"

#include "browser/BrowserMessageBuilder.h"
#include "browser/BrowserSettings.h"
#include "core/Group.h"
#include "core/Tools.h"
#include "crypto/Crypto.h"

#include <QJsonObject>
#include <QTest>

#include <botan/sodium.h>

using namespace Botan::Sodium;

QTEST_GUILESS_MAIN(TestBrowser)

const QString PUBLICKEY = "UIIPObeoya1G8g1M5omgyoPR/j1mR1HlYHu0wHCgMhA=";
const QString SECRETKEY = "B8ei4ZjQJkWzZU2SK/tBsrYRwp+6ztEMf5GFQV+i0yI=";
const QString SERVERPUBLICKEY = "lKnbLhrVCOqzEjuNoUz1xj9EZlz8xeO4miZBvLrUPVQ=";
const QString SERVERSECRETKEY = "tbPQcghxfOgbmsnEqG2qMIj1W2+nh+lOJcNsHncaz1Q=";
const QString NONCE = "zBKdvTjL5bgWaKMCTut/8soM/uoMrFoZ";
const QString CLIENTID = "testClient";

void TestBrowser::initTestCase()
{
    QVERIFY(Crypto::init());
    m_browserService = browserService();
    browserSettings()->setBestMatchOnly(false);
}

void TestBrowser::init()
{
    m_browserAction.reset(new BrowserAction());
}

/**
 * Tests for BrowserAction
 */

void TestBrowser::testChangePublicKeys()
{
    QJsonObject json;
    json["action"] = "change-public-keys";
    json["publicKey"] = PUBLICKEY;
    json["nonce"] = NONCE;

    auto response = m_browserAction->processClientMessage(nullptr, json);
    QCOMPARE(response["action"].toString(), QString("change-public-keys"));
    QCOMPARE(response["publicKey"].toString() == PUBLICKEY, false);
    QCOMPARE(response["success"].toString(), TRUE_STR);
}

void TestBrowser::testEncryptMessage()
{
    QJsonObject message;
    message["action"] = "test-action";

    m_browserAction->m_publicKey = SERVERPUBLICKEY;
    m_browserAction->m_secretKey = SERVERSECRETKEY;
    m_browserAction->m_clientPublicKey = PUBLICKEY;
    auto encrypted = browserMessageBuilder()->encryptMessage(message, NONCE, PUBLICKEY, SERVERSECRETKEY);

    QCOMPARE(encrypted, QString("+zjtntnk4rGWSl/Ph7Vqip/swvgeupk4lNgHEm2OO3ujNr0OMz6eQtGwjtsj+/rP"));
}

void TestBrowser::testDecryptMessage()
{
    QString message = "+zjtntnk4rGWSl/Ph7Vqip/swvgeupk4lNgHEm2OO3ujNr0OMz6eQtGwjtsj+/rP";
    m_browserAction->m_publicKey = SERVERPUBLICKEY;
    m_browserAction->m_secretKey = SERVERSECRETKEY;
    m_browserAction->m_clientPublicKey = PUBLICKEY;
    auto decrypted = browserMessageBuilder()->decryptMessage(message, NONCE, PUBLICKEY, SERVERSECRETKEY);

    QCOMPARE(decrypted["action"].toString(), QString("test-action"));
}

void TestBrowser::testGetBase64FromKey()
{
    unsigned char pk[crypto_box_PUBLICKEYBYTES];

    for (unsigned int i = 0; i < crypto_box_PUBLICKEYBYTES; ++i) {
        pk[i] = i;
    }

    auto response = browserMessageBuilder()->getBase64FromKey(pk, crypto_box_PUBLICKEYBYTES);
    QCOMPARE(response, QString("AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="));
}

void TestBrowser::testIncrementNonce()
{
    auto result = browserMessageBuilder()->incrementNonce(NONCE);
    QCOMPARE(result, QString("zRKdvTjL5bgWaKMCTut/8soM/uoMrFoZ"));
}

/**
 * Tests for BrowserService
 */
void TestBrowser::testTopLevelDomain()
{
    QString url1 = "https://another.example.co.uk";
    QString url2 = "https://www.example.com";
    QString url3 = "http://test.net";
    QString url4 = "http://so.many.subdomains.co.jp";
    QString url5 = "https://192.168.0.1";
    QString url6 = "https://192.168.0.1:8000";

    QString res1 = m_browserService->getTopLevelDomainFromUrl(url1);
    QString res2 = m_browserService->getTopLevelDomainFromUrl(url2);
    QString res3 = m_browserService->getTopLevelDomainFromUrl(url3);
    QString res4 = m_browserService->getTopLevelDomainFromUrl(url4);
    QString res5 = m_browserService->getTopLevelDomainFromUrl(url5);
    QString res6 = m_browserService->getTopLevelDomainFromUrl(url6);

    QCOMPARE(res1, QString("example.co.uk"));
    QCOMPARE(res2, QString("example.com"));
    QCOMPARE(res3, QString("test.net"));
    QCOMPARE(res4, QString("subdomains.co.jp"));
    QCOMPARE(res5, QString("192.168.0.1"));
    QCOMPARE(res6, QString("192.168.0.1"));
}

void TestBrowser::testIsIpAddress()
{
    auto host1 = "example.com"; // Not valid
    auto host2 = "192.168.0.1";
    auto host3 = "278.21.2.0"; // Not valid
    auto host4 = "2001:0db8:85a3:0000:0000:8a2e:0370:7334";
    auto host5 = "2001:db8:0:1:1:1:1:1";
    auto host6 = "fe80::1ff:fe23:4567:890a";
    auto host7 = "2001:20::1";
    auto host8 = "2001:0db8:85y3:0000:0000:8a2e:0370:7334"; // Not valid

    QVERIFY(!m_browserService->isIpAddress(host1));
    QVERIFY(m_browserService->isIpAddress(host2));
    QVERIFY(!m_browserService->isIpAddress(host3));
    QVERIFY(m_browserService->isIpAddress(host4));
    QVERIFY(m_browserService->isIpAddress(host5));
    QVERIFY(m_browserService->isIpAddress(host6));
    QVERIFY(m_browserService->isIpAddress(host7));
    QVERIFY(!m_browserService->isIpAddress(host8));
}

void TestBrowser::testSortPriority()
{
    QFETCH(QString, entryUrl);
    QFETCH(QString, siteUrl);
    QFETCH(QString, formUrl);
    QFETCH(int, expectedScore);

    QScopedPointer<Entry> entry(new Entry());
    entry->setUrl(entryUrl);

    QCOMPARE(m_browserService->sortPriority(m_browserService->getEntryURLs(entry.data()), siteUrl, formUrl),
             expectedScore);
}

void TestBrowser::testSortPriority_data()
{
    const QString siteUrl = "https://github.com/login";
    const QString formUrl = "https://github.com/session";

    QTest::addColumn<QString>("entryUrl");
    QTest::addColumn<QString>("siteUrl");
    QTest::addColumn<QString>("formUrl");
    QTest::addColumn<int>("expectedScore");

    QTest::newRow("Exact Match") << siteUrl << siteUrl << siteUrl << 100;
    QTest::newRow("Exact Match (site)") << siteUrl << siteUrl << formUrl << 100;
    QTest::newRow("Exact Match (form)") << siteUrl << "https://github.net" << siteUrl << 100;
    QTest::newRow("Exact Match No Trailing Slash") << "https://github.com"
                                                   << "https://github.com/" << formUrl << 100;
    QTest::newRow("Exact Match No Scheme") << "github.com/login" << siteUrl << formUrl << 100;
    QTest::newRow("Exact Match with Query") << "https://github.com/login?test=test#fragment"
                                            << "https://github.com/login?test=test" << formUrl << 100;

    QTest::newRow("Site Query Mismatch") << siteUrl << siteUrl + "?test=test" << formUrl << 90;

    QTest::newRow("Path Mismatch (site)") << "https://github.com/" << siteUrl << formUrl << 85;
    QTest::newRow("Path Mismatch (site) No Scheme") << "github.com" << siteUrl << formUrl << 85;
    QTest::newRow("Path Mismatch (form)") << "https://github.com/"
                                          << "https://github.net" << formUrl << 85;
    QTest::newRow("Path Mismatch (diff parent)") << "https://github.com/keepassxreboot" << siteUrl << formUrl << 80;
    QTest::newRow("Path Mismatch (diff parent, form)") << "https://github.com/keepassxreboot"
                                                       << "https://github.net" << formUrl << 70;

    QTest::newRow("Subdomain Mismatch (site)") << siteUrl << "https://sub.github.com/"
                                               << "https://github.net/" << 60;
    QTest::newRow("Subdomain Mismatch (form)") << siteUrl << "https://github.net/"
                                               << "https://sub.github.com/" << 50;

    QTest::newRow("Scheme Mismatch") << "http://github.com" << siteUrl << formUrl << 0;
    QTest::newRow("Scheme Mismatch w/path") << "http://github.com/login" << siteUrl << formUrl << 0;
    QTest::newRow("Invalid URL") << "http://github" << siteUrl << formUrl << 0;
}

void TestBrowser::testSearchEntries()
{
    auto db = QSharedPointer<Database>::create();
    auto* root = db->rootGroup();

    QStringList urls = {"https://github.com/login_page",
                        "https://github.com/login",
                        "https://github.com/",
                        "github.com/login",
                        "http://github.com",
                        "http://github.com/login",
                        "github.com",
                        "github.com/login",
                        "https://github", // Invalid URL
                        "github.com"};

    createEntries(urls, root);

    browserSettings()->setMatchUrlScheme(false);
    auto result =
        m_browserService->searchEntries(db, "https://github.com", "https://github.com/session"); // db, url, submitUrl

    QCOMPARE(result.length(), 9);
    QCOMPARE(result[0]->url(), QString("https://github.com/login_page"));
    QCOMPARE(result[1]->url(), QString("https://github.com/login"));
    QCOMPARE(result[2]->url(), QString("https://github.com/"));
    QCOMPARE(result[3]->url(), QString("github.com/login"));
    QCOMPARE(result[4]->url(), QString("http://github.com"));
    QCOMPARE(result[5]->url(), QString("http://github.com/login"));

    // With matching there should be only 3 results + 4 without a scheme
    browserSettings()->setMatchUrlScheme(true);
    result = m_browserService->searchEntries(db, "https://github.com", "https://github.com/session");
    QCOMPARE(result.length(), 7);
    QCOMPARE(result[0]->url(), QString("https://github.com/login_page"));
    QCOMPARE(result[1]->url(), QString("https://github.com/login"));
    QCOMPARE(result[2]->url(), QString("https://github.com/"));
    QCOMPARE(result[3]->url(), QString("github.com/login"));
}

void TestBrowser::testSearchEntriesByPath()
{
    auto db = QSharedPointer<Database>::create();
    auto* root = db->rootGroup();

    QStringList urlsRoot = {"https://root.example.com/", "root.example.com/login"};
    auto entriesRoot = createEntries(urlsRoot, root);

    auto* groupLevel1 = new Group();
    groupLevel1->setParent(root);
    groupLevel1->setName("TestGroup1");
    QStringList urlsLevel1 = {"https://1.example.com/", "1.example.com/login"};
    auto entriesLevel1 = createEntries(urlsLevel1, groupLevel1);

    auto* groupLevel2 = new Group();
    groupLevel2->setParent(groupLevel1);
    groupLevel2->setName("TestGroup2");
    QStringList urlsLevel2 = {"https://2.example.com/", "2.example.com/login"};
    auto entriesLevel2 = createEntries(urlsLevel2, groupLevel2);

    compareEntriesByPath(db, entriesRoot, "");
    compareEntriesByPath(db, entriesLevel1, "TestGroup1/");
    compareEntriesByPath(db, entriesLevel2, "TestGroup1/TestGroup2/");
}

void TestBrowser::compareEntriesByPath(QSharedPointer<Database> db, QList<Entry*> entries, QString path)
{
    for (Entry* entry : entries) {
        QString testUrl = "keepassxc://by-path/" + path + entry->title();
        /* Look for an entry with that path. First using handleEntry, then through the search */
        QCOMPARE(m_browserService->shouldIncludeEntry(entry, testUrl, ""), true);
        auto result = m_browserService->searchEntries(db, testUrl, "");
        QCOMPARE(result.length(), 1);
        QCOMPARE(result[0], entry);
    }
}

void TestBrowser::testSearchEntriesByUUID()
{
    auto db = QSharedPointer<Database>::create();
    auto* root = db->rootGroup();

    /* The URLs don't really matter for this test, we just need some entries */
    QStringList urls = {"https://github.com/login_page",
                        "https://github.com/login",
                        "https://github.com/",
                        "github.com/login",
                        "http://github.com",
                        "http://github.com/login",
                        "github.com",
                        "github.com/login",
                        "https://github",
                        "github.com",
                        "",
                        "not an URL"};
    auto entries = createEntries(urls, root);

    for (Entry* entry : entries) {
        QString testUrl = "keepassxc://by-uuid/" + entry->uuidToHex();
        /* Look for an entry with that UUID. First using handleEntry, then through the search */
        QCOMPARE(m_browserService->shouldIncludeEntry(entry, testUrl, ""), true);
        auto result = m_browserService->searchEntries(db, testUrl, "");
        QCOMPARE(result.length(), 1);
        QCOMPARE(result[0], entry);
    }

    /* Test for entries that don't exist */
    QStringList uuids = {"00000000000000000000000000000000",
                         "00000000000000000000000000000001",
                         "00000000000000000000000000000002/",
                         "invalid uuid",
                         "000000000000000000000000000000000000000"
                         "00000000000000000000000"};

    for (QString uuid : uuids) {
        QString testUrl = "keepassxc://by-uuid/" + uuid;

        for (Entry* entry : entries) {
            QCOMPARE(m_browserService->shouldIncludeEntry(entry, testUrl, ""), false);
        }

        auto result = m_browserService->searchEntries(db, testUrl, "");
        QCOMPARE(result.length(), 0);
    }
}

void TestBrowser::testSearchEntriesWithPort()
{
    auto db = QSharedPointer<Database>::create();
    auto* root = db->rootGroup();

    QStringList urls = {"http://127.0.0.1:443", "http://127.0.0.1:80"};

    createEntries(urls, root);

    auto result = m_browserService->searchEntries(db, "http://127.0.0.1:443", "http://127.0.0.1");
    QCOMPARE(result.length(), 1);
    QCOMPARE(result[0]->url(), QString("http://127.0.0.1:443"));
}

void TestBrowser::testSearchEntriesWithAdditionalURLs()
{
    auto db = QSharedPointer<Database>::create();
    auto* root = db->rootGroup();

    QStringList urls = {"https://github.com/", "https://www.example.com", "http://domain.com"};

    auto entries = createEntries(urls, root);

    // Add an additional URL to the first entry
    entries.first()->attributes()->set(BrowserService::ADDITIONAL_URL, "https://keepassxc.org");

    auto result = m_browserService->searchEntries(db, "https://github.com", "https://github.com/session");
    QCOMPARE(result.length(), 1);
    QCOMPARE(result[0]->url(), QString("https://github.com/"));

    // Search the additional URL. It should return the same entry
    auto additionalResult = m_browserService->searchEntries(db, "https://keepassxc.org", "https://keepassxc.org");
    QCOMPARE(additionalResult.length(), 1);
    QCOMPARE(additionalResult[0]->url(), QString("https://github.com/"));
}

void TestBrowser::testInvalidEntries()
{
    auto db = QSharedPointer<Database>::create();
    auto* root = db->rootGroup();
    const QString url("https://github.com");
    const QString submitUrl("https://github.com/session");

    QStringList urls = {
        "https://github.com/login",
        "https:///github.com/", // Extra '/'
        "http://github.com/**//*",
        "http://*.github.com/login",
        "//github.com", // fromUserInput() corrects this one.
        "github.com/{}<>",
        "http:/example.com",
    };

    createEntries(urls, root);

    browserSettings()->setMatchUrlScheme(true);
    auto result = m_browserService->searchEntries(db, "https://github.com", "https://github.com/session");
    QCOMPARE(result.length(), 2);
    QCOMPARE(result[0]->url(), QString("https://github.com/login"));
    QCOMPARE(result[1]->url(), QString("//github.com"));

    // Test the URL's directly
    QCOMPARE(m_browserService->handleURL(urls[0], url, submitUrl), true);
    QCOMPARE(m_browserService->handleURL(urls[1], url, submitUrl), false);
    QCOMPARE(m_browserService->handleURL(urls[2], url, submitUrl), false);
    QCOMPARE(m_browserService->handleURL(urls[3], url, submitUrl), false);
    QCOMPARE(m_browserService->handleURL(urls[4], url, submitUrl), true);
    QCOMPARE(m_browserService->handleURL(urls[5], url, submitUrl), false);
}

void TestBrowser::testSubdomainsAndPaths()
{
    auto db = QSharedPointer<Database>::create();
    auto* root = db->rootGroup();

    QStringList urls = {
        "https://www.github.com/login/page.xml",
        "https://login.github.com/",
        "https://github.com",
        "http://www.github.com",
        "http://login.github.com/pathtonowhere",
        ".github.com", // Invalid URL
        "www.github.com/",
        "https://github", // Invalid URL
        "https://hub.com" // Should not return
    };

    createEntries(urls, root);

    browserSettings()->setMatchUrlScheme(false);
    auto result = m_browserService->searchEntries(db, "https://github.com", "https://github.com/session");
    QCOMPARE(result.length(), 1);
    QCOMPARE(result[0]->url(), QString("https://github.com"));

    // With www subdomain
    result = m_browserService->searchEntries(db, "https://www.github.com", "https://www.github.com/session");
    QCOMPARE(result.length(), 4);
    QCOMPARE(result[0]->url(), QString("https://www.github.com/login/page.xml"));
    QCOMPARE(result[1]->url(), QString("https://github.com")); // Accepts any subdomain
    QCOMPARE(result[2]->url(), QString("http://www.github.com"));
    QCOMPARE(result[3]->url(), QString("www.github.com/"));

    // With www subdomain omitted
    root->setCustomDataTriState(BrowserService::OPTION_OMIT_WWW, Group::Enable);
    result = m_browserService->searchEntries(db, "https://github.com", "https://github.com/session");
    root->setCustomDataTriState(BrowserService::OPTION_OMIT_WWW, Group::Inherit);
    QCOMPARE(result.length(), 4);
    QCOMPARE(result[0]->url(), QString("https://www.github.com/login/page.xml"));
    QCOMPARE(result[1]->url(), QString("https://github.com"));
    QCOMPARE(result[2]->url(), QString("http://www.github.com"));
    QCOMPARE(result[3]->url(), QString("www.github.com/"));

    // With scheme matching there should be only 1 result
    browserSettings()->setMatchUrlScheme(true);
    result = m_browserService->searchEntries(db, "https://github.com", "https://github.com/session");
    QCOMPARE(result.length(), 1);
    QCOMPARE(result[0]->url(), QString("https://github.com"));

    // Test site with subdomain in the site URL
    QStringList entryURLs = {
        "https://accounts.example.com",
        "https://accounts.example.com/path",
        "https://subdomain.example.com/",
        "https://another.accounts.example.com/",
        "https://another.subdomain.example.com/",
        "https://example.com/",
        "https://example" // Invalid URL
    };

    createEntries(entryURLs, root);

    result = m_browserService->searchEntries(db, "https://accounts.example.com/", "https://accounts.example.com/");
    QCOMPARE(result.length(), 3);
    QCOMPARE(result[0]->url(), QString("https://accounts.example.com"));
    QCOMPARE(result[1]->url(), QString("https://accounts.example.com/path"));
    QCOMPARE(result[2]->url(), QString("https://example.com/")); // Accepts any subdomain

    result = m_browserService->searchEntries(
        db, "https://another.accounts.example.com/", "https://another.accounts.example.com/");
    QCOMPARE(result.length(), 4);
    QCOMPARE(result[0]->url(),
             QString("https://accounts.example.com")); // Accepts any subdomain under accounts.example.com
    QCOMPARE(result[1]->url(), QString("https://accounts.example.com/path"));
    QCOMPARE(result[2]->url(), QString("https://another.accounts.example.com/"));
    QCOMPARE(result[3]->url(), QString("https://example.com/")); // Accepts one or more subdomains

    // Test local files. It should be a direct match.
    QStringList localFiles = {"file:///Users/testUser/tests/test.html"};

    createEntries(localFiles, root);

    // With local files, url is always set to the file scheme + ://. Submit URL holds the actual URL.
    result = m_browserService->searchEntries(db, "file://", "file:///Users/testUser/tests/test.html");
    QCOMPARE(result.length(), 1);
}

QList<Entry*> TestBrowser::createEntries(QStringList& urls, Group* root) const
{
    QList<Entry*> entries;
    for (int i = 0; i < urls.length(); ++i) {
        auto entry = new Entry();
        entry->setGroup(root);
        entry->beginUpdate();
        entry->setUrl(urls[i]);
        entry->setUsername(QString("User %1").arg(i));
        entry->setUuid(QUuid::createUuid());
        entry->setTitle(QString("Name_%1").arg(entry->uuidToHex()));
        entry->endUpdate();
        entries.push_back(entry);
    }

    return entries;
}
void TestBrowser::testValidURLs()
{
    QHash<QString, bool> urls;
    urls["https://github.com/login"] = true;
    urls["https:///github.com/"] = false;
    urls["http://github.com/**//*"] = false;
    urls["http://*.github.com/login"] = false;
    urls["//github.com"] = true;
    urls["github.com/{}<>"] = false;
    urls["http:/example.com"] = false;
    urls["cmd://C:/Toolchains/msys2/usr/bin/mintty \"ssh jon@192.168.0.1:22\""] = true;
    urls["file:///Users/testUser/Code/test.html"] = true;
    urls["{REF:A@I:46C9B1FFBD4ABC4BBB260C6190BAD20C} "] = true;

    QHashIterator<QString, bool> i(urls);
    while (i.hasNext()) {
        i.next();
        QCOMPARE(Tools::checkUrlValid(i.key()), i.value());
    }
}

void TestBrowser::testBestMatchingCredentials()
{
    auto db = QSharedPointer<Database>::create();
    auto* root = db->rootGroup();

    // Test with simple URL entries
    QStringList urls = {"https://github.com/loginpage", "https://github.com/justsomepage", "https://github.com/"};

    auto entries = createEntries(urls, root);

    browserSettings()->setBestMatchOnly(true);

    QString siteUrl = "https://github.com/loginpage";
    auto result = m_browserService->searchEntries(db, siteUrl, siteUrl);
    auto sorted = m_browserService->sortEntries(result, siteUrl, siteUrl);
    QCOMPARE(sorted.size(), 1);
    QCOMPARE(sorted[0]->url(), siteUrl);

    siteUrl = "https://github.com/justsomepage";
    result = m_browserService->searchEntries(db, siteUrl, siteUrl);
    sorted = m_browserService->sortEntries(result, siteUrl, siteUrl);
    QCOMPARE(sorted.size(), 1);
    QCOMPARE(sorted[0]->url(), siteUrl);

    siteUrl = "https://github.com/";
    result = m_browserService->searchEntries(db, siteUrl, siteUrl);
    sorted = m_browserService->sortEntries(entries, siteUrl, siteUrl);
    QCOMPARE(sorted.size(), 1);
    QCOMPARE(sorted[0]->url(), siteUrl);

    // Without best-matching the URL with the path should be returned first
    browserSettings()->setBestMatchOnly(false);
    siteUrl = "https://github.com/loginpage";
    result = m_browserService->searchEntries(db, siteUrl, siteUrl);
    sorted = m_browserService->sortEntries(result, siteUrl, siteUrl);
    QCOMPARE(sorted.size(), 3);
    QCOMPARE(sorted[0]->url(), siteUrl);

    // Test with subdomains
    QStringList subdomainsUrls = {"https://sub.github.com/loginpage",
                                  "https://sub.github.com/justsomepage",
                                  "https://bus.github.com/justsomepage",
                                  "https://subdomain.example.com/",
                                  "https://subdomain.example.com",
                                  "https://example.com"};

    entries = createEntries(subdomainsUrls, root);

    browserSettings()->setBestMatchOnly(true);
    siteUrl = "https://sub.github.com/justsomepage";
    result = m_browserService->searchEntries(db, siteUrl, siteUrl);
    sorted = m_browserService->sortEntries(result, siteUrl, siteUrl);
    QCOMPARE(sorted.size(), 1);
    QCOMPARE(sorted[0]->url(), siteUrl);

    siteUrl = "https://github.com/justsomepage";
    result = m_browserService->searchEntries(db, siteUrl, siteUrl);
    sorted = m_browserService->sortEntries(result, siteUrl, siteUrl);
    QCOMPARE(sorted.size(), 1);
    QCOMPARE(sorted[0]->url(), siteUrl);

    siteUrl = "https://sub.github.com/justsomepage?wehavesomeextra=here";
    result = m_browserService->searchEntries(db, siteUrl, siteUrl);
    sorted = m_browserService->sortEntries(result, siteUrl, siteUrl);
    QCOMPARE(sorted.size(), 1);
    QCOMPARE(sorted[0]->url(), QString("https://sub.github.com/justsomepage"));

    // The matching should not care if there's a / path or not.
    siteUrl = "https://subdomain.example.com/";
    result = m_browserService->searchEntries(db, siteUrl, siteUrl);
    sorted = m_browserService->sortEntries(result, siteUrl, siteUrl);
    QCOMPARE(sorted.size(), 2);
    QCOMPARE(sorted[0]->url(), QString("https://subdomain.example.com"));
    QCOMPARE(sorted[1]->url(), QString("https://subdomain.example.com/"));

    // Entries with https://example.com should be still returned even if the site URL has a subdomain. Those have the
    // best match.
    db = QSharedPointer<Database>::create();
    root = db->rootGroup();
    QStringList domainUrls = {"https://example.com", "https://example.com", "https://other.example.com"};
    entries = createEntries(domainUrls, root);
    siteUrl = "https://subdomain.example.com";
    result = m_browserService->searchEntries(db, siteUrl, siteUrl);
    sorted = m_browserService->sortEntries(result, siteUrl, siteUrl);

    QCOMPARE(sorted.size(), 2);
    QCOMPARE(sorted[0]->url(), QString("https://example.com"));
    QCOMPARE(sorted[1]->url(), QString("https://example.com"));

    // https://github.com/keepassxreboot/keepassxc/issues/4754
    db = QSharedPointer<Database>::create();
    root = db->rootGroup();
    QStringList fooUrls = {"https://example.com/foo", "https://example.com/bar"};
    entries = createEntries(fooUrls, root);

    for (const auto& url : fooUrls) {
        result = m_browserService->searchEntries(db, url, url);
        sorted = m_browserService->sortEntries(result, url, url);
        QCOMPARE(sorted.size(), 1);
        QCOMPARE(sorted[0]->url(), QString(url));
    }

    // https://github.com/keepassxreboot/keepassxc/issues/4734
    db = QSharedPointer<Database>::create();
    root = db->rootGroup();
    QStringList testUrls = {"http://some.domain.tld/somePath", "http://some.domain.tld/otherPath"};
    entries = createEntries(testUrls, root);

    for (const auto& url : testUrls) {
        result = m_browserService->searchEntries(db, url, url);
        sorted = m_browserService->sortEntries(result, url, url);
        QCOMPARE(sorted.size(), 1);
        QCOMPARE(sorted[0]->url(), QString(url));
    }
}

void TestBrowser::testBestMatchingWithAdditionalURLs()
{
    auto db = QSharedPointer<Database>::create();
    auto* root = db->rootGroup();

    QStringList urls = {"https://github.com/loginpage", "https://test.github.com/", "https://github.com/"};

    auto entries = createEntries(urls, root);
    browserSettings()->setBestMatchOnly(true);

    // Add an additional URL to the first entry
    entries.first()->attributes()->set(BrowserService::ADDITIONAL_URL, "https://test.github.com/anotherpage");

    // The first entry should be triggered
    auto result = m_browserService->searchEntries(
        db, "https://test.github.com/anotherpage", "https://test.github.com/anotherpage");
    auto sorted = m_browserService->sortEntries(
        result, "https://test.github.com/anotherpage", "https://test.github.com/anotherpage");
    QCOMPARE(sorted.length(), 1);
    QCOMPARE(sorted[0]->url(), urls[0]);
}

void TestBrowser::testIsUrlIdentical()
{
    QVERIFY(browserService()->isUrlIdentical("https://example.com", "https://example.com"));
    QVERIFY(browserService()->isUrlIdentical("https://example.com", "  https://example.com  "));
    QVERIFY(!browserService()->isUrlIdentical("https://example.com", "https://example2.com"));
    QVERIFY(!browserService()->isUrlIdentical("https://example.com/", "https://example.com/#login"));
    QVERIFY(browserService()->isUrlIdentical("https://example.com", "https://example.com/"));
    QVERIFY(browserService()->isUrlIdentical("https://example.com/", "https://example.com"));
    QVERIFY(browserService()->isUrlIdentical("https://example.com/  ", "  https://example.com"));
    QVERIFY(!browserService()->isUrlIdentical("https://example.com/", "  example.com"));
    QVERIFY(browserService()->isUrlIdentical("https://example.com/path/to/nowhere",
                                             "https://example.com/path/to/nowhere/"));
    QVERIFY(!browserService()->isUrlIdentical("https://example.com/", "://example.com/"));
    QVERIFY(browserService()->isUrlIdentical("ftp://127.0.0.1/", "ftp://127.0.0.1"));
}
