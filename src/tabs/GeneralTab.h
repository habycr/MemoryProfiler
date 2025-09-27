#pragma once
#include <QWidget>

#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

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
    // métricas de cabecera
    QLabel* heapCur_  = nullptr;
    QLabel* heapPeak_ = nullptr;
    QLabel* activeAllocs_ = nullptr;  // NUEVO
    QLabel* leakMb_       = nullptr;  // NUEVO
    QLabel* totalAllocs_  = nullptr;  // NUEVO

    // charts existentes
    QChartView*  allocChart_  = nullptr;
    QLineSeries* allocSeries_ = nullptr;

    QChartView*  freeChart_   = nullptr;
    QLineSeries* freeSeries_  = nullptr;

    // NUEVO: serie MB vs tiempo
    QChartView*  memChart_   = nullptr;
    QLineSeries* memSeries_  = nullptr;

    double t_ = 0.0; // tiempo en segundos para la serie
};

