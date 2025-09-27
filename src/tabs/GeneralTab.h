#pragma once
#include <QWidget>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

// En Qt5 las clases están en el namespace QtCharts y hay que habilitarlo;
// en Qt6 normalmente están disponibles sin prefijo.
#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  #include <QtCharts/QChartGlobal>
  QT_CHARTS_USE_NAMESPACE
#endif

class QLabel;
struct MetricsSnapshot;

class GeneralTab : public QWidget {
    Q_OBJECT
  public:
    explicit GeneralTab(QWidget* parent=nullptr);
    void updateSnapshot(const MetricsSnapshot& s);
private:
    QLabel* heapCur_  = nullptr;
    QLabel* heapPeak_ = nullptr;

    QChartView*  allocChart_  = nullptr;   // <- SIN QtCharts::
    QLineSeries* allocSeries_ = nullptr;

    QChartView*  freeChart_   = nullptr;
    QLineSeries* freeSeries_  = nullptr;

    int t_ = 0;
};
