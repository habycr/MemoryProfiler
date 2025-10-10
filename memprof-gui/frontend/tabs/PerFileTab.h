#pragma once
#include <QWidget>
#include <QSortFilterProxyModel>
#include "frontend/model/TableModels.h"
#include "memprof/proto/MetricsSnapshot.h"

class QTableView;
class QLabel;

class PerFileTab : public QWidget {
    Q_OBJECT
public:
    explicit PerFileTab(QWidget* parent=nullptr);
    void updateSnapshot(const MetricsSnapshot& s);

private:
    PerFileModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QLabel* totalRows_ = nullptr;
};
