#pragma once
#include <QWidget>
#include <QSortFilterProxyModel>
#include <QtCharts/QChartView>
#include <QtCharts/QPieSeries>
#include <QtCharts/QBarSeries>
#include <QtCharts/QBarSet>
#include <QtCharts/QBarCategoryAxis>
#include <QtCharts/QScatterSeries>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  #include <QtCharts/QChartGlobal>
  QT_CHARTS_USE_NAMESPACE
#endif

#include "../../frontend/model/TableModels.h"
#include "../include/memprof/proto/MetricsSnapshot.h"

class QTableView; class QLineEdit; class QPushButton; class QLabel;

class LeaksTab : public QWidget {
    Q_OBJECT
public:
    explicit LeaksTab(QWidget* parent=nullptr);
    void updateSnapshot(const MetricsSnapshot& s);
private slots:
    void onCopySelected();

private:
    // Filtro + tabla
    LeaksModel* model_ = nullptr;
    QSortFilterProxyModel* proxy_ = nullptr;
    QTableView* table_ = nullptr;
    QLineEdit* filterEdit_ = nullptr;
    QPushButton* copyBtn_ = nullptr;

    // Resumen KPIs
    QLabel* leakTotalLbl_   = nullptr;
    QLabel* largestLbl_     = nullptr;
    QLabel* topFileLbl_     = nullptr;
    QLabel* leakRateLbl_    = nullptr;

    // Charts
    QChartView* barsView_   = nullptr;   // barras por archivo (bytes)
    QChartView* pieView_    = nullptr;   // pastel distribución por archivo
    QChartView* timeView_   = nullptr;   // temporal detección

    // Helpers
    void rebuildCharts(const MetricsSnapshot& s);
};
