// SPDX-License-Identifier: GPL-3.0-only
// Compiled only into the explicitly branded prototype; no store mutations.
#include "desktop/mainwindow.h"
#include "desktop/settingsdialog.h"
#include "desktop/brickowldialog.h"
#include "common/actionmanager.h"
#include "common/brickowl.h"
#include "common/bricklinkstoreapi.h"
#include "utility/exception.h"
#include <QApplication>
#include <QAction>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QLineEdit>
#include <QStackedWidget>
#include <QTreeWidget>
#include <QSslSocket>
#include <QImageReader>
#include <QQmlEngine>
#include <QQmlComponent>
#include <memory>

int runBrickOwlSmoke()
{
    QJsonArray checks;
    bool passed = true;
    auto check = [&](const char *name, bool ok) {
        checks.append(QJsonObject{{u"check"_qs, QString::fromLatin1(name)}, {u"passed"_qs, ok}});
        passed &= ok;
    };
    auto *main = MainWindow::inst();
    QApplication::processEvents();
    check("main window opens", main && main->isVisible());
    auto *actions = ActionManager::inst();
    for (const auto *name : {"document_import_bl_store_inv", "document_import_bl_xml",
                             "document_import_bo_store_inv", "document_sync_marketplaces"}) {
        const auto *action = actions->qAction(name);
        check(name, action && action->isVisible());
    }
    {
        SettingsDialog settings(u"brickowl"_qs, main);
        settings.show();
        QApplication::processEvents();
        auto *page = settings.findChild<QWidget *>(u"brickowl"_qs);
        check("BrickOwl settings visible", page && page->isVisible());
        auto *key = page ? page->findChild<QLineEdit *>() : nullptr;
        if (key) key->setText(u"offline-smoke-test-not-a-real-key"_qs);
        check("API key editable and masked", key && key->text() == u"offline-smoke-test-not-a-real-key"
              && key->echoMode() == QLineEdit::Password);
        bool hasBrickLink = false;
        for (auto *tree : settings.findChildren<QTreeWidget *>())
            for (int i = 0; i < tree->topLevelItemCount(); ++i)
                hasBrickLink |= tree->topLevelItem(i)->text(0).contains(u"BrickLink");
        check("BrickLink settings retained", hasBrickLink);
        settings.grab().save(u"brickowl-settings-smoke.png"_qs);
        settings.reject(); // Never persist the dummy key.
    }
    {
        BrickOwlDialog import(nullptr, main);
        import.show();
        QApplication::processEvents();
        check("BrickOwl import dialog opens", import.isVisible());
        import.reject();
    }
    try {
        const auto lots = BrickOwl::parseInventory(QJsonDocument::fromJson(
            R"([{"lot_id":"501","boid":"12345","qty":"10","price":"0.1250","full_con":"new","for_sale":"1"}])"));
        check("inventory parser", lots.size() == 1 && lots.first().quantity == 10);
        bool rejected = false;
        try { BrickOwl::integer(QJsonValue(-1)); } catch (const Exception &) { rejected = true; }
        check("negative quantity rejected", rejected);
        const QJsonObject credentials {{u"consumerKey"_qs,u"dpf43f3p2l4k3l03"_qs},
            {u"consumerSecret"_qs,u"kd94hf93k423kf44"_qs},{u"token"_qs,u"nnch734d00sl2jdk"_qs},
            {u"tokenSecret"_qs,u"pfkkdhi9sl3r4s00"_qs}};
        const auto header = BrickLink::StoreApi::authorization("GET",
            QUrl(u"http://photos.example.net/photos?file=vacation.jpg&size=original"_qs),
            credentials, u"kllo9940pd9333jh"_qs, u"1191242096"_qs);
        check("OAuth known signature", header.contains("tR3%2BTy81lMeYAr%2FFid0kMTYa%2FWM%3D"));
    } catch (const Exception &) { check("offline parser checks", false); }
    check("TLS backend available", QSslSocket::supportsSsl());
#ifdef Q_OS_WIN
    check("native Windows platform", QGuiApplication::platformName() == u"windows");
    check("Windows Schannel TLS", QSslSocket::availableBackends().contains(u"schannel"_qs));
#endif
    const auto formats = QImageReader::supportedImageFormats();
    check("PNG and SVG plugins", formats.contains("png") && formats.contains("svg"));
    {
        QQmlEngine engine;
        QQmlComponent component(&engine);
        component.setData("import QtQuick\nimport QtQuick3D\nimport QtQml.WorkerScript\nItem { Node {} }", QUrl());
        std::unique_ptr<QObject> item(component.create());
        check("deployed QtQuick and Quick3D QML imports", item != nullptr && !component.isError());
    }
    main->grab().save(u"brickstore-main-smoke.png"_qs);
    QFile report(u"brickowl-smoke.json"_qs);
    if (!report.open(QIODevice::WriteOnly)) return 2;
    const QByteArray result = QJsonDocument(QJsonObject{{u"passed"_qs, passed}, {u"checks"_qs, checks},
        {u"platform"_qs, QGuiApplication::platformName()}}).toJson();
    if (report.write(result) != result.size()) return 2;
    return passed ? 0 : 1;
}
