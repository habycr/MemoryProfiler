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
    // --- KPIs de cabecera ---
    QLabel* heapCur_       = nullptr;
    QLabel* heapPeak_      = nullptr;
    QLabel* activeAllocs_  = nullptr;
    QLabel* leakMb_        = nullptr;
    QLabel* totalAllocs_   = nullptr;

    // --- Charts: allocs/s y frees/s (se mantienen como en tu versión) ---
    QChartView*  allocChart_   = nullptr;
    QLineSeries* allocSeries_  = nullptr;

    QChartView*  freeChart_    = nullptr;
    QLineSeries* freeSeries_   = nullptr;

    // --- Memoria vs tiempo (MB) — corregido para evitar doble trazo ---
    QChartView*  memChartView_ = nullptr;   // vista
    QChart*      memChart_     = nullptr;   // chart real
    QLineSeries* memSeries_    = nullptr;   // ÚNICA serie
    QValueAxis*  axX_mem_      = nullptr;   // eje X persistente
    QValueAxis*  axY_mem_      = nullptr;   // eje Y persistente

    // Top 3 por archivo (archivo | allocs | MB)
    QTableWidget* top3_ = nullptr;

    double t_ = 0.0; // tiempo en segundos para la serie
};
