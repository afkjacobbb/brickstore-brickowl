// SPDX-License-Identifier: GPL-3.0-only
#include <QtTest>
#include <QJsonArray>
#include "common/brickowl.h"
#include "common/bricklinkstoreapi.h"
#include "utility/exception.h"
class BrickOwlTests : public QObject
{
    Q_OBJECT
private slots:
    void inventoryStringsAndNumbers()
    {
        const auto doc = QJsonDocument::fromJson(R"([{"lot_id":"501","boid":"12345","qty":"10","price":"0.1250","full_con":"new","for_sale":"1"},{"lot_id":502,"boid":"12346","qty":2,"price":0.25,"con":"usedg","for_sale":false}])");
        const auto lots = BrickOwl::parseInventory(doc);
        QCOMPARE(lots.size(), 2);
        QCOMPARE(lots[0].quantity, 10);
        QCOMPARE(lots[0].price, .125);
        QVERIFY(lots[0].forSale);
        QCOMPARE(lots[1].price, .25);
        QVERIFY(!lots[1].forSale);
    }
    void invalidQuantityFailsClosed()
    {
        QVERIFY_EXCEPTION_THROWN(BrickOwl::integer(QJsonValue(-1)), Exception);
        QVERIFY_EXCEPTION_THROWN(BrickOwl::integer(QJsonValue(1.5)), Exception);
        QVERIFY_EXCEPTION_THROWN(BrickOwl::integer(QJsonValue()), Exception);
        QVERIFY_EXCEPTION_THROWN(BrickOwl::integer(QJsonValue(u"9999999999999"_qs)), Exception);
    }
    void errorEnvelopeIsNotEmptyInventory()
    {
        QVERIFY_EXCEPTION_THROWN(BrickOwl::parseInventory(QJsonDocument::fromJson(R"({"error":"bad key"})")), Exception);
    }
    void colorNamespacesAreSeparate()
    {
        const auto map = BrickOwl::parseColors(QJsonDocument::fromJson(R"({"99":{"id":"99","bl_ids":["5"]},"100":{"id":100,"bl_ids":["7","8"]}})"));
        QCOMPARE(map.value(99), 5u);
        QVERIFY(!map.contains(5));
        QVERIFY(!map.contains(100));
    }
    void itemCrossReferenceMustBeUnique()
    {
        QCOMPARE(BrickOwl::brickLinkId(QJsonDocument::fromJson(R"({"ids":[{"type":"design_id","id":"wrong"},{"type":"bl_item_no","id":"3001"}]})").object()), QByteArray("3001"));
        QVERIFY(BrickOwl::brickLinkId(QJsonDocument::fromJson(R"({"ids":[{"type":"bl_item_no","id":"3001"},{"type":"bl_item_no","id":"3001a"}]})").object()).isEmpty());
    }
    void conditionsArePreserved()
    {
        BrickLink::Lot lot;
        QVERIFY(BrickOwl::applyCondition(lot, u"usedg"_qs));
        QCOMPARE(int(lot.condition()), int(BrickLink::Condition::Used));
        QCOMPARE(lot.brickOwlCondition(), u"usedg"_qs);
        QVERIFY(BrickOwl::applyCondition(lot, u"news"_qs));
        QCOMPARE(int(lot.subCondition()), int(BrickLink::SubCondition::Sealed));
        QVERIFY(!BrickOwl::applyCondition(lot, u"mystery"_qs));
    }
    void separateIdsSurviveCopy()
    {
        BrickLink::Lot a;
        a.setLotId(42); a.setBrickOwlLotId(u"900"_qs); a.setBrickOwlId(u"300001"_qs);
        a.setBrickOwlColorId(99); a.setBrickOwlCondition(u"usedg"_qs);
        BrickLink::Lot b(a);
        QCOMPARE(b.lotId(), 42u); QCOMPARE(b.brickOwlLotId(), u"900"_qs); QVERIFY(a == b);
        b.setBrickOwlLotId(u"901"_qs); QVERIFY(!(a == b));

    }
    void oauthMatchesPublishedVector()
    {
        const QJsonObject credentials {{u"consumerKey"_qs,u"dpf43f3p2l4k3l03"_qs},{u"consumerSecret"_qs,u"kd94hf93k423kf44"_qs},{u"token"_qs,u"nnch734d00sl2jdk"_qs},{u"tokenSecret"_qs,u"pfkkdhi9sl3r4s00"_qs}};
        const auto header = BrickLink::StoreApi::authorization("GET", QUrl(u"http://photos.example.net/photos?file=vacation.jpg&size=original"_qs), credentials, u"kllo9940pd9333jh"_qs, u"1191242096"_qs);
        QVERIFY(header.contains("oauth_signature=\"tR3%2BTy81lMeYAr%2FFid0kMTYa%2FWM%3D\""));
        QVERIFY(!header.contains("kd94hf93k423kf44"));
    }
};
QTEST_GUILESS_MAIN(BrickOwlTests)
#include "tst_brickowl.moc"
