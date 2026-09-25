// SPDX-License-Identifier: GPL-3.0-only
#include "bricklinkstoreapi.h"
#include "brickowl.h"
#include "bricklink/core.h"
#include "utility/exception.h"
#include "utility/transfer.h"
#include <QJsonDocument>
#include <QJsonArray>
#include <QMessageAuthenticationCode>
#include <QNetworkReply>
#include <QUrlQuery>
#include <QUuid>
#include <QCoro/QCoroNetworkReply>

namespace BrickLink {
StoreApi::StoreApi(const QString &credentials, QObject *parent)
    : QObject(parent), m_credentials(QJsonDocument::fromJson(credentials.toUtf8()).object()) { }
QByteArray StoreApi::authorization(const QByteArray &method, const QUrl &url,
                                  const QJsonObject &c, const QString &nonce, const QString &timestamp)
{
    auto enc = [](const QString &s) { return QUrl::toPercentEncoding(s); };
    QMap<QString, QString> oauth {
        {u"oauth_consumer_key"_qs, c.value(u"consumerKey"_qs).toString()},
        {u"oauth_token"_qs, c.value(u"token"_qs).toString()},
        {u"oauth_nonce"_qs, nonce}, {u"oauth_timestamp"_qs, timestamp},
        {u"oauth_signature_method"_qs, u"HMAC-SHA1"_qs}, {u"oauth_version"_qs, u"1.0"_qs}
    };
    QList<QPair<QByteArray, QByteArray>> pairs;
    for (auto it = oauth.cbegin(); it != oauth.cend(); ++it) pairs.append({enc(it.key()), enc(it.value())});
    const auto query = QUrlQuery(url).queryItems(QUrl::FullyDecoded);
    for (const auto &p : query) pairs.append({enc(p.first), enc(p.second)});
    std::sort(pairs.begin(), pairs.end());
    QByteArray normalized;
    for (const auto &p : pairs) {
        if (!normalized.isEmpty()) normalized += '&';
        normalized += p.first + '=' + p.second;
    }
    auto base = url; base.setQuery(QString()); base.setFragment(QString());
    const QByteArray signatureBase = method + '&' + enc(base.toString(QUrl::FullyEncoded))
        + '&' + enc(QString::fromLatin1(normalized));
    const QByteArray key = enc(c.value(u"consumerSecret"_qs).toString()) + '&' + enc(c.value(u"tokenSecret"_qs).toString());
    oauth.insert(u"oauth_signature"_qs, QString::fromLatin1(QMessageAuthenticationCode::hash(signatureBase, key, QCryptographicHash::Sha1).toBase64()));
    QByteArray result = "OAuth ";
    for (auto it = oauth.cbegin(); it != oauth.cend(); ++it) {
        if (it != oauth.cbegin()) result += ", ";
        result += enc(it.key()) + "=\"" + enc(it.value()) + '"';
    }
    return result;
}
void StoreApi::cancel()
{
    m_cancelled = true;
    const auto replies = m_network.findChildren<QNetworkReply *>();
    for (auto *reply : replies) reply->abort();
}
QCoro::Task<QJsonValue> StoreApi::request(const QString &path, const QJsonObject &body)
{
    if (m_cancelled) throw Exception("Operation cancelled");
    for (const auto &key : {u"consumerKey"_qs, u"consumerSecret"_qs, u"token"_qs, u"tokenSecret"_qs})
        if (m_credentials.value(key).toString().isEmpty()) throw Exception("Configure all four BrickLink Store API credentials in Settings");
    const bool write = !body.isEmpty();
    const QUrl url(u"https://api.bricklink.com/api/store/v1/"_qs + path);
    QNetworkRequest req(url);
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("User-Agent", Transfer::defaultUserAgent().toUtf8());
    req.setRawHeader("Authorization", authorization(write ? "PUT" : "GET", url, m_credentials,
        QUuid::createUuid().toString(QUuid::Id128), QString::number(QDateTime::currentSecsSinceEpoch())));
    req.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_qs);
    QNetworkReply *reply = co_await (write ? m_network.put(req, QJsonDocument(body).toJson(QJsonDocument::Compact)) : m_network.get(req));
    const int code = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = reply->error();
    const auto bytes = reply->readAll();
    reply->deleteLater();
    if (error != QNetworkReply::NoError || code < 200 || code >= 300)
        throw Exception("BrickLink request failed (HTTP %1). Check credentials and registered IP. Refresh before retrying a write.").arg(code);
    QJsonParseError pe;
    const auto doc = QJsonDocument::fromJson(bytes, &pe);
    const int apiCode = doc.object().value(u"meta"_qs).toObject().value(u"code"_qs).toInt();
    if (pe.error != QJsonParseError::NoError || apiCode < 200 || apiCode >= 300)
        throw Exception("BrickLink returned an invalid response or an API error");
    co_return doc.object().value(u"data"_qs);
}
QCoro::Task<QList<Lot>> StoreApi::inventory()
{
    const auto data = co_await request(u"inventories"_qs);
    if (!data.isArray()) throw Exception("Invalid BrickLink inventory response");
    QList<Lot> lots;
    QSet<uint> ids;
    for (const auto &value : data.toArray()) {
        const auto o = value.toObject();
        if (o.value(u"is_stock_room"_qs).toBool() || o.value(u"is_reserved"_qs).toBool()) continue;
        const auto i = o.value(u"item"_qs).toObject();
        const auto type = i.value(u"type"_qs).toString();
        const QHash<QString, char> types {{u"PART"_qs,'P'}, {u"SET"_qs,'S'}, {u"MINIFIG"_qs,'M'}, {u"INSTRUCTION"_qs,'I'}, {u"BOX"_qs,'O'}};
        const auto item = core()->item(types.value(type), i.value(u"no"_qs).toString().toLatin1());
        const auto color = core()->color(uint(BrickOwl::integer(o.value(u"color_id"_qs))));
        if (!item || !color) continue;
        const auto condition = o.value(u"new_or_used"_qs).toString();
        if (condition != u"N" && condition != u"U") throw Exception("Invalid BrickLink condition");
        Lot lot(item, color);
        lot.setLotId(uint(BrickOwl::integer(o.value(u"inventory_id"_qs))));
        if (!lot.lotId() || ids.contains(lot.lotId())) throw Exception("Invalid or duplicate BrickLink lot ID");
        ids.insert(lot.lotId());
        lot.setQuantity(BrickOwl::integer(o.value(u"quantity"_qs)));
        lot.setCondition(condition == u"N" ? Condition::New : Condition::Used);
        const auto completeness = o.value(u"completeness"_qs).toString();
        lot.setSubCondition(completeness == u"S" ? SubCondition::Sealed
            : completeness == u"C" ? SubCondition::Complete
            : completeness == u"B" ? SubCondition::Incomplete : SubCondition::None);
        lots.append(lot);
    }
    co_return lots;
}
QCoro::Task<> StoreApi::updateQuantity(uint lotId, int expected, int target)
{
    if (!lotId || target < 0) throw Exception("Invalid lot ID or quantity");
    const QString path = u"inventories/"_qs + QString::number(lotId);
    const auto o = (co_await request(path)).toObject();
    if (BrickOwl::integer(o.value(u"quantity"_qs)) != expected)
        throw Exception("BrickLink quantity changed after preview; refresh before applying");
    if (expected == target) co_return;
    const int delta = target - expected;
    const QString value = (delta >= 0 ? u"+"_qs : QString()) + QString::number(delta);
    const QJsonObject body {{u"quantity"_qs, value}};
    co_await request(path, body);
}
}
