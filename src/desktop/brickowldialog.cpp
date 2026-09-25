// SPDX-License-Identifier: GPL-3.0-only
#include "brickowldialog.h"
#include "common/brickowl.h"
#include "common/bricklinkstoreapi.h"
#include "common/config.h"
#include "common/document.h"
#include "common/documentmodel.h"
#include "bricklink/io.h"
#include "utility/exception.h"
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QComboBox>
#include <QLineEdit>
#include <QCheckBox>
#include <QMessageBox>
#include <QRegularExpression>
#include <QCryptographicHash>
#include <QTimer>

BrickOwlDialog::BrickOwlDialog(Document *document, QWidget *parent)
    : QDialog(parent), m_document(document), m_import(!document)
{
    setWindowTitle(m_import ? tr("Import BrickOwl Store Inventory") : tr("Synchronize BrickLink ↔ BrickOwl"));
    resize(1020, 600);
    auto *layout = new QVBoxLayout(this);
    auto *info = new QLabel(m_import
        ? tr("Download your BrickOwl inventory into a normal BrickStore document. Only exact catalogue and color references are imported; unmatched lots are reported.")
        : tr("Compare the current document with both stores. Choose the source of the target quantities, review the checked rows, then apply. No orders are processed automatically."), this);
    info->setWordWrap(true);
    layout->addWidget(info);
    auto *options = new QHBoxLayout;
    m_target = new QComboBox(this);
    m_target->addItems({tr("Choose quantity source…"), tr("Current document"), tr("BrickLink"), tr("BrickOwl")});
    options->addWidget(m_target);
    m_create = new QCheckBox(tr("Create missing BrickOwl lots (new parts only)"), this);
    options->addWidget(m_create);
    m_currency = new QLineEdit(this);
    m_currency->setPlaceholderText(tr("BrickOwl currency, e.g. EUR"));
    m_currency->setMaxLength(3);
    options->addWidget(m_currency);
    layout->addLayout(options);
    m_target->setVisible(!m_import);
    m_create->setVisible(!m_import);
    m_currency->setVisible(m_import);
    m_quietStores = new QCheckBox(tr("Both stores are paused for sales; prices for new lots use the same currency as this document."), this);
    m_quietStores->setVisible(!m_import);
    layout->addWidget(m_quietStores);
    m_table = new QTableWidget(this);
    m_table->setColumnCount(7);
    m_table->setHorizontalHeaderLabels({tr("Apply"), tr("Item / color / condition"), tr("BrickLink"), tr("BrickOwl"), tr("Document"), tr("Target"), tr("Status")});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setStretchLastSection(true);
    layout->addWidget(m_table, 1);
    m_status = new QLabel(this);
    m_status->setTextFormat(Qt::PlainText);
    m_status->setWordWrap(true);
    layout->addWidget(m_status);
    auto *buttons = new QHBoxLayout;
    m_refresh = new QPushButton(tr("Download / refresh preview"), this);
    m_apply = new QPushButton(m_import ? tr("Import checked lots") : tr("Apply checked changes"), this);
    auto *close = new QPushButton(tr("Close"), this);
    m_apply->setEnabled(false);
    buttons->addWidget(m_refresh);
    buttons->addStretch();
    buttons->addWidget(m_apply);
    buttons->addWidget(close);
    layout->addLayout(buttons);
    connect(close, &QPushButton::clicked, this, &BrickOwlDialog::reject);
    connect(m_refresh, &QPushButton::clicked, this, [this]() -> QCoro::Task<> { co_await preview(); });
    connect(m_apply, &QPushButton::clicked, this, [this]() -> QCoro::Task<> { co_await apply(); });
    auto invalidate = [this] { m_apply->setEnabled(false); m_status->setText(tr("Refresh the preview after changing options.")); };
    connect(m_target, &QComboBox::currentIndexChanged, this, invalidate);
    connect(m_create, &QCheckBox::toggled, this, invalidate);
    connect(m_currency, &QLineEdit::textChanged, this, invalidate);
}
void BrickOwlDialog::reject()
{
    if (m_busy) {
        if (m_bo) m_bo->cancel();
        if (m_bl) m_bl->cancel();
        m_status->setText(tr("Cancelling… A submitted write may already have reached the server."));
        return; // Keep coroutine owners alive until outstanding requests finish.
    }
    QDialog::reject();
}
void BrickOwlDialog::busy(bool on)
{
    m_busy = on;
    m_refresh->setEnabled(!on);
    m_apply->setEnabled(false);
    m_target->setEnabled(!on);
    m_create->setEnabled(!on);
    m_currency->setEnabled(!on);
    m_quietStores->setEnabled(!on);
}
void BrickOwlDialog::render()
{
    m_table->setRowCount(int(m_rows.size()));
    for (int i = 0; i < m_rows.size(); ++i) {
        const auto &r = m_rows.at(i);
        auto *check = new QTableWidgetItem;
        check->setFlags(r.ready ? Qt::ItemIsEnabled | Qt::ItemIsUserCheckable : Qt::NoItemFlags);
        check->setCheckState(r.ready ? Qt::Checked : Qt::Unchecked);
        m_table->setItem(i, 0, check);
        const auto &l = r.mapped;
        const QString name = QString::fromLatin1(l.itemId()) + u" · " + l.itemName() + u" / " + l.colorName()
            + u" / " + (l.condition() == BrickLink::Condition::New ? tr("New") : tr("Used"));
        const QStringList cells { name, r.brickLink < 0 ? u"—"_qs : QString::number(r.brickLink),
            r.brickOwl < 0 ? u"—"_qs : QString::number(r.brickOwl),
            r.local ? QString::number(r.snapshot.quantity()) : u"—"_qs, QString::number(r.target), r.status };
        for (int c = 0; c < cells.size(); ++c) m_table->setItem(i, c + 1, new QTableWidgetItem(cells.at(c)));
    }
}
QCoro::Task<> BrickOwlDialog::preview()
{
    if (m_busy) co_return;
    if (m_import && !QRegularExpression(u"^[A-Z]{3}$"_qs).match(m_currency->text()).hasMatch()) {
        m_status->setText(tr("Enter the actual three-letter BrickOwl store currency. Prices are not converted.")); co_return;
    }
    if (!m_import && (!m_document || !m_target->currentIndex())) {
        m_status->setText(tr("Choose the source of the target quantities first.")); co_return;
    }
    busy(true);
    m_rows.clear();
    delete m_bo; delete m_bl;
    m_bo = new BrickOwl::Client(Config::inst()->brickOwlApiKey(), this);
    m_bl = new BrickLink::StoreApi(Config::inst()->brickLinkStoreApiCredentials(), this);
    m_status->setText(tr("Downloading current inventories…"));
    int skipped = 0;
    try {
        const auto remote = co_await m_bo->inventory();
        QList<BrickLink::Lot> boLots;
        QSet<QString> unresolvedIds;
        for (const auto &entry : remote) {
            try { boLots.append(co_await m_bo->toLot(entry)); }
            catch (const Exception &e) {
                ++skipped; unresolvedIds.insert(entry.lotId);
                if (m_import) {
                    Row r;
                    r.mapped.setBrickOwlId(entry.boid);
                    r.mapped.setBrickOwlLotId(entry.lotId);
                    r.brickOwl = entry.quantity;
                    r.status = tr("NOT MAPPED: BO lot %1 — %2").arg(entry.lotId, e.errorString());
                    m_rows.append(r);
                }
            }
        }
        if (m_import) {
            for (const auto &lot : boLots) {
                Row r; r.mapped = lot; r.target = lot.quantity(); r.brickOwl = lot.quantity(); r.ready = true;
                r.status = tr("Mapped — BO lot %1").arg(lot.brickOwlLotId()); m_rows.append(r);
            }
        } else {
            const auto blLots = co_await m_bl->inventory();
            const auto locals = m_document->model()->lots();
            for (auto *local : locals) {
                Row r; r.local = local; r.snapshot = *local; r.mapped = *local; r.target = local->quantity();
                try {
                    if (!local->item() || !local->color() || local->status() != BrickLink::Status::Include
                        || local->stockroom() != BrickLink::Stockroom::None || !local->reserved().isEmpty())
                        throw Exception("Excluded, reserved, stockroom or incomplete item");
                    QList<BrickLink::Lot> blMatches, boMatches;
                    for (const auto &l : blLots)
                        if ((local->lotId() ? l.lotId() == local->lotId() : BrickOwl::sameItem(*local, l)) && BrickOwl::sameItem(*local, l)) blMatches.append(l);
                    for (const auto &l : boLots)
                        if ((!local->brickOwlLotId().isEmpty() ? l.brickOwlLotId() == local->brickOwlLotId() : BrickOwl::sameItem(*local, l)) && BrickOwl::sameItem(*local, l)) boMatches.append(l);
                    if (blMatches.size() != 1) throw Exception("Missing or ambiguous BrickLink lot");
                    const auto &bl = blMatches.first();
                    r.mapped.setLotId(bl.lotId()); r.brickLink = bl.quantity();
                    if (boMatches.size() == 1) {
                        const auto &bo = boMatches.first();
                        if (bo.status() != BrickLink::Status::Include) throw Exception("BrickOwl lot is not for sale");
                        r.mapped.setBrickOwlLotId(bo.brickOwlLotId()); r.mapped.setBrickOwlId(bo.brickOwlId());
                        r.mapped.setBrickOwlColorId(bo.brickOwlColorId()); r.mapped.setBrickOwlCondition(bo.brickOwlCondition());
                        r.brickOwl = bo.quantity();
                    } else if (boMatches.isEmpty() && local->brickOwlLotId().isEmpty() && m_create->isChecked() && unresolvedIds.isEmpty()) {
                        r.mapped.setBrickOwlId(co_await m_bo->findBoid(*local));
                        const auto details = co_await m_bo->catalog(r.mapped.brickOwlId());
                        r.mapped.setBrickOwlColorId(BrickOwl::integer(details.value(u"color_id"_qs)));
                        r.mapped.setBrickOwlCondition(u"new"_qs);
                        r.create = true;
                    } else throw Exception("Missing/ambiguous BrickOwl lot or an unresolved catalogue mapping");
                    if (m_target->currentIndex() == 2) r.target = r.brickLink;
                    if (m_target->currentIndex() == 3) {
                        if (r.brickOwl < 0) throw Exception("Cannot use a missing BrickOwl lot as quantity source");
                        r.target = r.brickOwl;
                    }
                    if (r.target < 0 || (r.create && r.target == 0)) throw Exception("Invalid target quantity for this operation");
                    r.ready = true;
                    r.status = r.create ? tr("CREATE BrickOwl") : (r.target == r.brickLink && r.target == r.brickOwl) ? tr("OK") : tr("CHANGE");
                } catch (const Exception &e) { r.status = e.errorString(); }
                m_rows.append(r);
            }
        }
        // Reject every row in a duplicated mapping, not just the later occurrence.
        if (!m_import) {
            QHash<uint, int> blCounts;
            QHash<QString, int> boCounts;
            for (const auto &row : std::as_const(m_rows)) {
                if (row.mapped.lotId()) ++blCounts[row.mapped.lotId()];
                if (!row.mapped.brickOwlLotId().isEmpty()) ++boCounts[row.mapped.brickOwlLotId()];
            }
            for (auto &row : m_rows) {
                if ((row.mapped.lotId() && blCounts.value(row.mapped.lotId()) > 1)
                    || (!row.mapped.brickOwlLotId().isEmpty() && boCounts.value(row.mapped.brickOwlLotId()) > 1)) {
                    row.ready = false;
                    row.status = tr("Ambiguous duplicate document mapping");
                }
            }
        }
        render();
        m_status->setText(tr("Preview ready. %1 unmatched BrickOwl lots were skipped. Verify quantities and mappings before applying.").arg(skipped));
        busy(false);
        m_apply->setEnabled(std::any_of(m_rows.cbegin(), m_rows.cend(), [](const Row &r) { return r.ready; }));
    } catch (const Exception &e) {
        render(); busy(false); m_status->setText(e.errorString());
    }
}
QCoro::Task<> BrickOwlDialog::apply()
{
    if (m_busy) co_return;
    if (!m_import && !m_quietStores->isChecked()) {
        m_status->setText(tr("Pause sales in both stores and confirm the checkbox. Cross-marketplace writes are not atomic.")); co_return;
    }
    QList<int> selected;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows.at(i).ready && m_table->item(i, 0)->checkState() == Qt::Checked) selected.append(i);
    if (selected.isEmpty()) co_return;
    if (QMessageBox::question(this, windowTitle(), tr("Apply %1 checked rows? Store updates cannot be undone with Undo.").arg(selected.size())) != QMessageBox::Yes) co_return;
    if (m_import) {
        BrickLink::IO::ParseResult result;
        for (int i : selected) result.addLot(new BrickLink::Lot(m_rows.at(i).mapped));
        result.setCurrencyCode(m_currency->text());
        auto *doc = Document::create(new DocumentModel(std::move(result)));
        doc->setTitle(tr("BrickOwl Store Inventory"));
        accept(); co_return;
    }
    busy(true);
    int completed = 0;
    try {
        if (!m_document) throw Exception("The document was closed");
        // Refresh both inventories BEFORE any mutation and reject stale product/quantity data.
        const auto blNow = co_await m_bl->inventory();
        const auto boRemote = co_await m_bo->inventory();
        QList<BrickLink::Lot> boNow;
        for (const auto &entry : boRemote) boNow.append(co_await m_bo->toLot(entry));
        for (int i : selected) {
            const auto &r = m_rows.at(i);
            if (!m_document->model()->lots().contains(r.local) || !(*r.local == r.snapshot)) throw Exception("Document changed since preview");
            bool blOk = false, boOk = r.create;
            for (const auto &l : blNow)
                if (l.lotId() == r.mapped.lotId()) blOk = BrickOwl::sameItem(l, r.mapped) && l.quantity() == r.brickLink;
            for (const auto &l : boNow) {
                if (r.create && BrickOwl::sameItem(l, r.mapped)) throw Exception("A matching BrickOwl lot appeared since preview");
                if (!r.create && l.brickOwlLotId() == r.mapped.brickOwlLotId()) boOk = l.status() == BrickLink::Status::Include && BrickOwl::sameItem(l, r.mapped) && l.quantity() == r.brickOwl;
            }
            if (!blOk || !boOk) throw Exception("Inventory changed since preview. Download a new preview.");
        }
        for (int i : selected) {
            auto &r = m_rows[i];
            m_status->setText(tr("Applying row %1 of %2…").arg(completed + 1).arg(selected.size()));
            if (r.create) {
                // Durable intent in existing settings prevents blind replay after an ambiguous POST.
                const QString key = u"BrickOwl/UnconfirmedCreate/"_qs + QString::fromLatin1(QCryptographicHash::hash(
                    (Config::inst()->brickOwlApiKey() + r.mapped.brickOwlId() + QString::number(r.mapped.lotId())).toUtf8(), QCryptographicHash::Sha256).toHex());
                if (Config::inst()->contains(key)) throw Exception("An earlier create was not confirmed. Inspect your BrickOwl inventory and refresh; do not create the lot again blindly.");
                Config::inst()->setValue(key, true); Config::inst()->sync();
                if (Config::inst()->status() != QSettings::NoError) throw Exception("Could not record create intent; no request sent");
                auto created = r.mapped; created.setQuantity(r.target);
                r.mapped.setBrickOwlLotId(co_await m_bo->create(created));
                // Preserve the confirmed remote ID in the existing document before the second store write.
                auto local = *r.local;
                local.setBrickOwlLotId(r.mapped.brickOwlLotId()); local.setBrickOwlId(r.mapped.brickOwlId());
                local.setBrickOwlColorId(r.mapped.brickOwlColorId()); local.setBrickOwlCondition(r.mapped.brickOwlCondition());
                m_document->model()->changeLot(r.local, local);
                Config::inst()->remove(key); Config::inst()->sync();
            } else {
                co_await m_bo->updateQuantity(r.mapped.brickOwlLotId(), r.brickOwl, r.target);
            }
            co_await m_bl->updateQuantity(r.mapped.lotId(), r.brickLink, r.target);
            auto updated = r.mapped; updated.setQuantity(r.target);
            m_document->model()->changeLot(r.local, updated);
            r.status = tr("Applied; checking…"); ++completed;
        }
        const auto verifiedBl = co_await m_bl->inventory();
        const auto verifiedBo = co_await m_bo->inventory();
        for (int i : selected) {
            auto &r = m_rows[i]; bool blOk = false, boOk = false;
            for (const auto &l : verifiedBl) if (l.lotId() == r.mapped.lotId()) blOk = l.quantity() == r.target;
            for (const auto &l : verifiedBo) if (l.lotId == r.mapped.brickOwlLotId()) boOk = l.quantity == r.target;
            r.status = blOk && boOk ? tr("Verified in both stores") : tr("CONFLICT — refresh and review");
        }
        m_status->setText(tr("%1 rows applied. Check verification results and save the document to retain marketplace IDs.").arg(completed));
    } catch (const Exception &e) {
        m_status->setText(tr("Stopped after %1 completed rows: %2\nA store may already be changed. Refresh both stores before retrying; no automatic rollback was attempted.").arg(completed).arg(e.errorString()));
    }
    render(); busy(false); // Always require a fresh preview for another write.
}
#include "moc_brickowldialog.cpp"
