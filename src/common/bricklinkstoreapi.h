// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QCoro/QCoroTask>
#include "bricklink/lot.h"

namespace BrickLink {
// Optional Store API for quantity writes; existing BrickStore-token flows are unchanged.
class StoreApi : public QObject
{
public:
    explicit StoreApi(const QString &credentials, QObject *parent = nullptr);
    QCoro::Task<QList<Lot>> inventory();
    QCoro::Task<> updateQuantity(uint lotId, int expected, int target);
    void cancel();
    static QByteArray authorization(const QByteArray &method, const QUrl &url,
                                    const QJsonObject &credentials,
                                    const QString &nonce, const QString &timestamp);
private:
    QCoro::Task<QJsonValue> request(const QString &path, const QJsonObject &body = {});
    QJsonObject m_credentials;
    QNetworkAccessManager m_network;
    bool m_cancelled = false;
};
}
