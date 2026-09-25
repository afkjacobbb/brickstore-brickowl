// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QUrlQuery>
#include <QCoro/QCoroTask>
#include "bricklink/lot.h"

namespace BrickOwl {
struct Inventory {
    QString lotId, boid, condition, publicNote, personalNote;
    int quantity = 0;
    double price = 0;
    bool forSale = false;
};
// Pure parsers shared by live requests and fixture tests. Unknown data fails closed.
QString text(const QJsonValue &value);
int integer(const QJsonValue &value);
QList<Inventory> parseInventory(const QJsonDocument &document);
QHash<int, uint> parseColors(const QJsonDocument &document);
QByteArray brickLinkId(const QJsonObject &catalog);
char brickLinkType(const QString &type);
bool applyCondition(BrickLink::Lot &lot, const QString &condition);
bool sameItem(const BrickLink::Lot &a, const BrickLink::Lot &b);

class Client : public QObject
{
public:
    explicit Client(const QString &key, QObject *parent = nullptr);
    QCoro::Task<QJsonDocument> request(const QString &path, QUrlQuery parameters = {}, bool write = false);
    QCoro::Task<QList<Inventory>> inventory();
    QCoro::Task<QHash<int, uint>> colors();
    QCoro::Task<QJsonObject> catalog(const QString &boid);
    QCoro::Task<QString> findBoid(const BrickLink::Lot &lot);
    QCoro::Task<BrickLink::Lot> toLot(const Inventory &entry);
    QCoro::Task<> updateQuantity(const QString &lotId, int expected, int target);
    QCoro::Task<QString> create(const BrickLink::Lot &lot);
    QCoro::Task<> remove(const QString &lotId);
    void cancel();
private:
    QString m_key;
    QNetworkAccessManager m_network;
    QHash<QString, QJsonObject> m_catalog;
    QHash<int, uint> m_colors;
    bool m_haveColors = false;
    bool m_cancelled = false;
};
} // namespace BrickOwl
