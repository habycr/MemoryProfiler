#pragma once
#include <QWidget>
#include <QSortFilterProxyModel>
#include "model/TableModels.h"
#include "model/MetricsSnapshot.h"


class QTableView; class QLineEdit; class QPushButton;


class LeaksTab : public QWidget {
    Q_OBJECT
    public:
    explicit LeaksTab(QWidget* parent=nullptr);
    void updateSnapshot(const MetricsSnapshot& s);
private slots:
void onCopySelected();
private:
    LeaksModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QPushButton* copyBtn_ = nullptr;
};