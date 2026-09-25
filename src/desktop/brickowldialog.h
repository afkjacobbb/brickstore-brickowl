// SPDX-License-Identifier: GPL-3.0-only
#pragma once
#include <QDialog>
#include <QPointer>
#include <QCoro/QCoroTask>
#include "bricklink/lot.h"
class Document;
class QTableWidget;
class QLabel;
class QPushButton;
class QComboBox;
class QLineEdit;
class QCheckBox;
namespace BrickOwl { class Client; }
namespace BrickLink { class StoreApi; }

class BrickOwlDialog : public QDialog
{
    Q_OBJECT
public:
    explicit BrickOwlDialog(Document *document, QWidget *parent = nullptr);
    void reject() override;
private:
    struct Row {
        BrickLink::Lot *local = nullptr;
        BrickLink::Lot snapshot;
        BrickLink::Lot mapped;
        int brickLink = -1;
        int brickOwl = -1;
        int target = 0;
        QString status;
        bool ready = false;
        bool create = false;
    };
    QCoro::Task<> preview();
    QCoro::Task<> apply();
    void render();
    void busy(bool on);
    QPointer<Document> m_document;
    QList<Row> m_rows;
    BrickOwl::Client *m_bo = nullptr;
    BrickLink::StoreApi *m_bl = nullptr;
    QTableWidget *m_table;
    QLabel *m_status;
    QPushButton *m_refresh;
    QPushButton *m_apply;
    QComboBox *m_target;
    QLineEdit *m_currency;
    QCheckBox *m_create;
    QCheckBox *m_quietStores;
    bool m_busy = false;
    bool m_import = false;
};
