#pragma once
#include <QWidget>
#include <QSortFilterProxyModel>

#include <QtCharts/QChartView>
#include <QtCharts/QPieSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QLineSeries>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  #include <QtCharts/QChartGlobal>
  QT_CHARTS_USE_NAMESPACE
#endif

#include "frontend/model/TableModels.h"
#include "memprof/proto/MetricsSnapshot.h"

class QTableView;
class QLineEdit;
class QPushButton;
class QLabel;

class LeaksTab : public QWidget {
    Q_OBJECT
public:
    explicit LeaksTab(QWidget* parent=nullptr);
    void updateSnapshot(const MetricsSnapshot& s);

private slots:
    void onCopySelected();

private:
    LeaksModel*            model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QTableView*            table_ = nullptr;
    QLineEdit*             filterEdit_ = nullptr;
    QPushButton*           copyBtn_ = nullptr;

    QLabel* leakTotalLbl_ = nullptr;
    QLabel* largestLbl_   = nullptr;
    QLabel* topFileLbl_   = nullptr;
    QLabel* leakRateLbl_  = nullptr;

    QChartView* barsView_ = nullptr;
    QChartView* pieView_  = nullptr;
    QChartView* timeView_ = nullptr;

    void rebuildCharts(const MetricsSnapshot& s);
};
