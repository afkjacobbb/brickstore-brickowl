// SPDX-License-Identifier: GPL-3.0-only
#include "brickowl.h"
#include "bricklink/core.h"
#include "utility/exception.h"
#include "utility/transfer.h"
#include <QCoro/QCoroNetworkReply>
#include <QNetworkReply>
#include <QJsonArray>
#include <QRegularExpression>
#include <cmath>
#include <limits>

namespace BrickOwl {
QString text(const QJsonValue &v)
{
    return v.isString() ? v.toString() : v.isDouble() ? QString::number(v.toInteger()) : QString();
}
int integer(const QJsonValue &v)
{
    bool ok = false;
    const auto n = text(v).toLongLong(&ok);
    if (!ok || n < 0 || n > std::numeric_limits<int>::max()
            || (v.isDouble() && v.toDouble() != double(n)))
        throw Exception("Invalid or missing integer in marketplace response");
    return int(n);
}
static QString identifier(const QJsonValue &v)
{
    const auto s = text(v);
    if (!QRegularExpression(u"^[0-9]+$"_qs).match(s).hasMatch())
        throw Exception("Invalid or missing BrickOwl identifier");
    return s;
}
QList<Inventory> parseInventory(const QJsonDocument &doc)
{
    if (!doc.isArray())
        throw Exception("BrickOwl did not return an inventory array");
    QList<Inventory> result;
    QSet<QString> ids;
    for (const auto &value : doc.array()) {
        const auto o = value.toObject();
        Inventory i;
        i.lotId = identifier(o.value(u"lot_id"_qs));
        i.boid = identifier(o.value(u"boid"_qs));
        i.quantity = integer(o.value(u"qty"_qs));
        i.condition = o.value(u"full_con"_qs).toString();
        if (i.condition.isEmpty())
            i.condition = o.value(u"con"_qs).toString();
        bool ok = false;
        i.price = text(o.value(u"price"_qs)).toDouble(&ok);
        // Decimal strings must not be truncated by the identifier helper.
        if (o.value(u"price"_qs).isDouble()) {
            i.price = o.value(u"price"_qs).toDouble();
            ok = true;
        }
        if (!ok || !std::isfinite(i.price) || i.price < 0 || ids.contains(i.lotId))
            throw Exception("Invalid price or duplicate BrickOwl lot");
        const auto sale = o.value(u"for_sale"_qs);
        if (!sale.isBool() && text(sale) != u"0" && text(sale) != u"1")
            throw Exception("Missing BrickOwl for_sale flag");
        i.forSale = sale.isBool() ? sale.toBool() : text(sale) == u"1";
        i.publicNote = o.value(u"public_note"_qs).toString();
        i.personalNote = o.value(u"personal_note"_qs).toString();
        ids.insert(i.lotId);
        result.append(i);
    }
    return result;
}
QHash<int, uint> parseColors(const QJsonDocument &doc)
{
    if (!doc.isObject())
        throw Exception("BrickOwl color list is not an object");
    QHash<int, uint> result;
    const auto colors = doc.object();
    for (auto it = colors.begin(); it != colors.end(); ++it) {
        const auto o = it.value().toObject();
        const int id = integer(o.value(u"id"_qs));
        const auto bl = o.value(u"bl_ids"_qs).toArray();
        QSet<uint> candidates;
        for (const auto &v : bl)
            candidates.insert(uint(integer(v)));
        if (candidates.size() == 1)
            result.insert(id, *candidates.begin());
    }
    return result;
}
QByteArray brickLinkId(const QJsonObject &catalog)
{
    QSet<QByteArray> ids;
    for (const auto &v : catalog.value(u"ids"_qs).toArray()) {
        const auto id = v.toObject();
        if (id.value(u"type"_qs).toString() == u"bl_item_no")
            ids.insert(id.value(u"id"_qs).toString().toLatin1());
    }
    return ids.size() == 1 ? *ids.begin() : QByteArray();
}
char brickLinkType(const QString &type)
{
    if (type.compare(u"Part", Qt::CaseInsensitive) == 0) return 'P';
    if (type.compare(u"Set", Qt::CaseInsensitive) == 0) return 'S';
    if (type.compare(u"Minifigure", Qt::CaseInsensitive) == 0) return 'M';
    if (type.compare(u"Instructions", Qt::CaseInsensitive) == 0) return 'I';
    if (type.compare(u"Packaging", Qt::CaseInsensitive) == 0) return 'O';
    return 0;
}
bool applyCondition(BrickLink::Lot &lot, const QString &condition)
{
    using namespace BrickLink;
    static const QSet<QString> supported { u"new"_qs, u"news"_qs, u"newc"_qs, u"newi"_qs,
        u"usedc"_qs, u"usedi"_qs, u"usedn"_qs, u"usedg"_qs, u"useda"_qs };
    if (!supported.contains(condition)) return false;
    lot.setCondition(condition.startsWith(u"new") ? Condition::New : Condition::Used);
    lot.setSubCondition(condition.endsWith(u'c') ? SubCondition::Complete
        : condition.endsWith(u'i') ? SubCondition::Incomplete
        : condition == u"news" ? SubCondition::Sealed : SubCondition::None);
    lot.setBrickOwlCondition(condition);
    return true;
}
bool sameItem(const BrickLink::Lot &a, const BrickLink::Lot &b)
{
    return a.item() && b.item() && a.item() == b.item() && a.color() && a.color() == b.color()
        && a.condition() == b.condition() && a.subCondition() == b.subCondition()
        && (a.brickOwlCondition().isEmpty() || b.brickOwlCondition().isEmpty()
            || a.brickOwlCondition() == b.brickOwlCondition());
}
Client::Client(const QString &key, QObject *parent) : QObject(parent), m_key(key) { }
void Client::cancel()
{
    m_cancelled = true;
    const auto replies = m_network.findChildren<QNetworkReply *>();
    for (auto *reply : replies) reply->abort();
}
QCoro::Task<QJsonDocument> Client::request(const QString &path, QUrlQuery parameters, bool write)
{
    if (m_cancelled) throw Exception("Operation cancelled");
    if (m_key.isEmpty()) throw Exception("Configure the BrickOwl API key in Settings first");
    parameters.addQueryItem(u"key"_qs, m_key);
    QUrl url(u"https://api.brickowl.com/v1/"_qs + path);
    if (!write) url.setQuery(parameters);
    QNetworkRequest req(url);
    req.setTransferTimeout(20000);
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
    req.setRawHeader("User-Agent", Transfer::defaultUserAgent().toUtf8());
    req.setHeader(QNetworkRequest::ContentTypeHeader, u"application/x-www-form-urlencoded"_qs);
    // No automatic write retries: a timeout can mean the write already happened.
    QNetworkReply *reply = co_await (write
        ? m_network.post(req, parameters.query(QUrl::FullyEncoded).toUtf8().replace("+", "%2B"))
        : m_network.get(req));
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const auto error = reply->error();
    const QByteArray body = reply->readAll();
    reply->deleteLater();
    if (error != QNetworkReply::NoError || status < 200 || status >= 300) {
        if (write) throw Exception("BrickOwl write was not confirmed. Refresh the preview before retrying; the server may have applied it.");
        throw Exception("BrickOwl request failed (HTTP %1). Check the key, permissions and connection.").arg(status);
    }
    QJsonParseError parseError;
    const auto doc = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || doc.isNull())
        throw Exception("BrickOwl returned invalid JSON; refresh before retrying");
    const auto obj = doc.object();
    if (obj.contains(u"error"_qs) || (write && obj.value(u"status"_qs).toString() != u"success"))
        throw Exception("BrickOwl rejected the request. Check API permissions and the inventory values.");
    co_return doc;
}
QCoro::Task<QList<Inventory>> Client::inventory()
{
    co_return parseInventory(co_await request(u"inventory/list"_qs));
}
QCoro::Task<QHash<int, uint>> Client::colors()
{
    if (!m_haveColors) {
        m_colors = parseColors(co_await request(u"catalog/color_list"_qs));
        m_haveColors = true;
    }
    co_return m_colors;
}
QCoro::Task<QJsonObject> Client::catalog(const QString &boid)
{
    if (!m_catalog.contains(boid)) {
        const QUrlQuery query {{u"boid"_qs, boid}};
        const auto doc = co_await request(u"catalog/lookup"_qs, query);
        if (!doc.isObject()) throw Exception("Invalid BrickOwl catalogue response");
        m_catalog.insert(boid, doc.object());
    }
    co_return m_catalog.value(boid);
}
QCoro::Task<BrickLink::Lot> Client::toLot(const Inventory &entry)
{
    const auto details = co_await catalog(entry.boid);
    const auto colorMap = co_await colors();
    const auto id = brickLinkId(details);
    const char type = brickLinkType(details.value(u"type"_qs).toString());
    const int color = integer(details.value(u"color_id"_qs));
    if (id.isEmpty() || !type || !colorMap.contains(color))
        throw Exception("No unambiguous BrickLink item/color reference for BrickOwl lot %1").arg(entry.lotId);
    const auto *item = BrickLink::core()->item(type, id);
    const auto *blColor = BrickLink::core()->color(colorMap.value(color));
    if (!item || !blColor) throw Exception("Item/color is not in the local BrickStore catalogue");
    BrickLink::Lot lot(item, blColor);
    if (!applyCondition(lot, entry.condition)) throw Exception("Unsupported BrickOwl condition");
    lot.setBrickOwlId(entry.boid);
    lot.setBrickOwlColorId(color);
    lot.setBrickOwlLotId(entry.lotId);
    lot.setQuantity(entry.quantity);
    lot.setPrice(entry.price);
    lot.setComments(entry.publicNote);
    lot.setRemarks(entry.personalNote);
    lot.setStatus(entry.forSale ? BrickLink::Status::Include : BrickLink::Status::Exclude);
    // lotId stays zero: an external ID is not automatically a BrickLink lot ID.
    co_return lot;
}
QCoro::Task<QString> Client::findBoid(const BrickLink::Lot &lot)
{
    if (lot.itemTypeId() != 'P' || lot.condition() != BrickLink::Condition::New)
        throw Exception("Creating new BrickOwl lots currently supports new parts only");
    const auto colorMap = co_await colors();
    QList<int> boColors;
    for (auto it = colorMap.cbegin(); it != colorMap.cend(); ++it)
        if (it.value() == lot.colorId()) boColors.append(it.key());
    if (boColors.size() != 1) throw Exception("No unique BrickOwl color mapping");
    const QUrlQuery query {
        {u"id"_qs, QString::fromLatin1(lot.itemId())}, {u"type"_qs, u"Part"_qs},
        {u"id_type"_qs, u"bl_item_no"_qs}};
    const auto doc = co_await request(u"catalog/id_lookup"_qs, query);
    const auto ids = doc.object().value(u"boids"_qs).toArray();
    QStringList matches;
    for (const auto &v : ids) {
        const auto candidate = text(v);
        const auto detail = co_await catalog(candidate);
        if (brickLinkId(detail) == lot.itemId() && brickLinkType(detail.value(u"type"_qs).toString()) == lot.itemTypeId()
                && integer(detail.value(u"color_id"_qs)) == boColors.first())
            matches.append(candidate);
    }
    matches.removeDuplicates();
    if (matches.size() != 1) throw Exception("No unique BrickOwl catalogue match; no listing will be created");
    co_return matches.first();
}
QCoro::Task<> Client::updateQuantity(const QString &lotId, int expected, int target)
{
    if (target < 0) throw Exception("Negative inventory quantity");
    const QUrlQuery query {{u"lot_id"_qs, lotId}};
    const auto lots = parseInventory(co_await request(u"inventory/list"_qs, query));
    if (lots.size() != 1 || lots.first().lotId != lotId || lots.first().quantity != expected)
        throw Exception("BrickOwl quantity changed after preview. Refresh before applying.");
    if (expected == target) co_return;
    // Use a delta so a concurrent sale cannot be overwritten by an absolute quantity.
    const QUrlQuery update {{u"lot_id"_qs, lotId},
        {u"relative_quantity"_qs, QString::number(target - expected)}};
    co_await request(u"inventory/update"_qs, update, true);
}
QCoro::Task<QString> Client::create(const BrickLink::Lot &lot)
{
    if (lot.quantity() <= 0 || lot.brickOwlId().isEmpty() || lot.brickOwlColorId() < 0)
        throw Exception("Missing confirmed BrickOwl mapping or invalid quantity");
    const QUrlQuery query {
        {u"boid"_qs, lot.brickOwlId()}, {u"color_id"_qs, QString::number(lot.brickOwlColorId())},
        {u"quantity"_qs, QString::number(lot.quantity())},
        {u"price"_qs, QString::number(lot.price(), 'f', 4)},
        {u"condition"_qs, lot.brickOwlCondition()}};
    const auto doc = co_await request(u"inventory/create"_qs, query, true);
    co_return identifier(doc.object().value(u"lot_id"_qs));
}
QCoro::Task<> Client::remove(const QString &lotId)
{
    const QUrlQuery query {{u"lot_id"_qs, lotId}};
    co_await request(u"inventory/delete"_qs, query, true);
}
} // namespace BrickOwl
