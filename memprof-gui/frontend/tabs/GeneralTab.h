#pragma once
#include <QWidget>

#include <QtCharts/QChart>
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
  #include <QtCharts/QChartGlobal>
  QT_CHARTS_USE_NAMESPACE
#endif

class QLabel;
class QTableWidget;
struct MetricsSnapshot;

class GeneralTab : public QWidget {
    Q_OBJECT
public:
    explicit GeneralTab(QWidget* parent=nullptr);
    void updateSnapshot(const MetricsSnapshot& s);

private:
    // KPIs
    QLabel*       heapCur_      = nullptr;
    QLabel*       heapPeak_     = nullptr;
    QLabel*       activeAllocs_ = nullptr;
    QLabel*       leakMb_       = nullptr;
    QLabel*       totalAllocs_  = nullptr;

    // Chart (MB vs tiempo)
    QChartView*   memChartView_ = nullptr;
    QChart*       memChart_     = nullptr;
    QLineSeries*  memSeries_    = nullptr;
    QValueAxis*   axX_mem_      = nullptr;
    QValueAxis*   axY_mem_      = nullptr;

    // Top-3 por archivo
    QTableWidget* top3_ = nullptr;

    double t_ = 0.0; // tiempo en segundos
};
