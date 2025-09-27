#pragma once
#include <QWidget>
#include <QSortFilterProxyModel>
#include "model/TableModels.h"
#include "model/MetricsSnapshot.h"


class QTableView; class QLineEdit;


class PerFileTab : public QWidget {
    Q_OBJECT
    public:
    explicit PerFileTab(QWidget* parent=nullptr);
    void updateSnapshot(const MetricsSnapshot& s);
private:
    PerFileModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
};