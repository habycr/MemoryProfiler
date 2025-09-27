#include "GeneralTab.h"
#include "model/MetricsSnapshot.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QtMath>

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using namespace QtCharts; // Qt5
#endif

namespace {
static inline QString bytesToHuman(qint64 b) {
  if (b < 1024) return QString::number(b) + " B";
  double kb = b / 1024.0; if (kb < 1024.0) return QString::number(kb, 'f', 1) + " KB";
  double mb = kb / 1024.0; if (mb < 1024.0) return QString::number(mb, 'f', 1) + " MB";
  double gb = mb / 1024.0; return QString::number(gb, 'f', 2) + " GB";
}
} // namespace

GeneralTab::GeneralTab(QWidget* parent) : QWidget(parent) {
  auto* root = new QVBoxLayout(this);

  // ----- Métricas arriba -----
  heapCur_      = new QLabel("Heap actual: 0 B");
  heapPeak_     = new QLabel("Pico: 0 B");
  activeAllocs_ = new QLabel("Activas: 0");
  leakMb_       = new QLabel("Leaks: 0 MB");
  totalAllocs_  = new QLabel("Total allocs: 0");

  auto* topRow = new QHBoxLayout;
  topRow->addWidget(heapCur_);
  topRow->addWidget(heapPeak_);
  topRow->addWidget(activeAllocs_);
  topRow->addWidget(leakMb_);
  topRow->addWidget(totalAllocs_);
  topRow->addStretch(1);
  root->addLayout(topRow);

  // ----- Memoria vs tiempo (MB) -----
  memSeries_ = new QLineSeries();
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  memSeries_->setUseOpenGL(true);
#endif
  auto* memChart = new QChart();
  memChart->legend()->hide();
  memChart->addSeries(memSeries_);

  auto* axX_mem = new QValueAxis();
  axX_mem->setTitleText("t (s)");
  axX_mem->setRange(0.0, 60.0);
  auto* axY_mem = new QValueAxis();
  axY_mem->setTitleText("MB");
  axY_mem->setRange(0.0, 1.0);

  memChart->addAxis(axX_mem, Qt::AlignBottom);
  memChart->addAxis(axY_mem, Qt::AlignLeft);
  memSeries_->attachAxis(axX_mem);
  memSeries_->attachAxis(axY_mem);

  memChart_ = new QChartView(memChart);
  memChart_->setRenderHint(QPainter::Antialiasing);
  root->addWidget(memChart_);

  // ----- Chart de allocs/s -----
  allocSeries_ = new QLineSeries();
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  allocSeries_->setUseOpenGL(true);
#endif
  auto* allocChart = new QChart();
  allocChart->legend()->hide();
  allocChart->addSeries(allocSeries_);
  auto* axX1 = new QValueAxis(); axX1->setTitleText("t (s)"); axX1->setRange(0.0, 60.0);
  auto* axY1 = new QValueAxis(); axY1->setTitleText("allocs/s"); axY1->setRange(0.0, 1.0);
  allocChart->addAxis(axX1, Qt::AlignBottom); allocChart->addAxis(axY1, Qt::AlignLeft);
  allocSeries_->attachAxis(axX1); allocSeries_->attachAxis(axY1);
  allocChart_ = new QChartView(allocChart);
  allocChart_->setRenderHint(QPainter::Antialiasing);
  root->addWidget(allocChart_);

  // ----- Chart de frees/s -----
  freeSeries_ = new QLineSeries();
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
  freeSeries_->setUseOpenGL(true);
#endif
  auto* freeChart = new QChart();
  freeChart->legend()->hide();
  freeChart->addSeries(freeSeries_);
  auto* axX2 = new QValueAxis(); axX2->setTitleText("t (s)"); axX2->setRange(0.0, 60.0);
  auto* axY2 = new QValueAxis(); axY2->setTitleText("frees/s"); axY2->setRange(0.0, 1.0);
  freeChart->addAxis(axX2, Qt::AlignBottom); freeChart->addAxis(axY2, Qt::AlignLeft);
  freeSeries_->attachAxis(axX2); freeSeries_->attachAxis(axY2);
  freeChart_ = new QChartView(freeChart);
  freeChart_->setRenderHint(QPainter::Antialiasing);
  root->addWidget(freeChart_);
}

void GeneralTab::updateSnapshot(const MetricsSnapshot& s) {
  // ----- Etiquetas principales -----
  heapCur_->setText(QString("Heap actual: %1").arg(bytesToHuman(s.heapCurrent)));
  heapPeak_->setText(QString("Pico: %1").arg(bytesToHuman(s.heapPeak)));

  // Calcular métricas si no vinieran en el snapshot
  qint64 leakBytes = s.leakBytes;
  if (leakBytes <= 0) {
    for (const auto& L : s.leaks) leakBytes += L.size;
  }

  qint64 totalAllocs = s.totalAllocs;
  qint64 activeAllocs = s.activeAllocs;
  if (totalAllocs <= 0 || activeAllocs <= 0) {
    qint64 total = 0, act = 0;
    for (const auto& f : s.perFile) {
      total += f.allocs;
      act   += (f.allocs - f.frees);
    }
    if (totalAllocs <= 0)  totalAllocs  = total;
    if (activeAllocs <= 0) activeAllocs = act;
  }

  activeAllocs_->setText(QString("Activas: %1").arg(activeAllocs));
  leakMb_->setText(QString("Leaks: %1 MB").arg(leakBytes / (1024.0 * 1024.0), 0, 'f', 2));
  totalAllocs_->setText(QString("Total allocs: %1").arg(totalAllocs));

  // ----- Serie Memoria vs tiempo (MB) -----
  // Preferimos usar uptimeMs si viene; si no, avanzamos a paso fijo (~0.25s)
  double nextX = (s.uptimeMs > 0) ? (s.uptimeMs / 1000.0) : (t_ + 0.25);
  if (nextX <= t_) nextX = t_ + 0.25;
  t_ = nextX;

  const double memMB = s.heapCurrent / (1024.0 * 1024.0);
  memSeries_->append(t_, memMB);

  // Mantener una ventana de ~120 puntos/segundos
  const double WINDOW_S = 120.0;
  while (memSeries_->count() > 0 && memSeries_->at(0).x() < t_ - WINDOW_S) {
    memSeries_->removePoints(0, 1);
  }

  // Ajustar ejes de memoria
  auto* axX_mem = qobject_cast<QValueAxis*>(memChart_->chart()->axisX());
  auto* axY_mem = qobject_cast<QValueAxis*>(memChart_->chart()->axisY());
  if (axX_mem) axX_mem->setRange(qMax(0.0, t_ - WINDOW_S), t_);
  if (axY_mem) {
    double yMax = qMax(axY_mem->max(), qMax(1.0, memMB * 1.2));
    axY_mem->setRange(0.0, yMax);
  }

  // ----- Series de allocs/s y frees/s -----
  allocSeries_->append(t_, s.allocRate);
  freeSeries_->append(t_, s.freeRate);

  // Recorte ventana
  while (allocSeries_->count() > 0 && allocSeries_->at(0).x() < t_ - WINDOW_S) {
    allocSeries_->removePoints(0, 1);
  }
  while (freeSeries_->count() > 0 && freeSeries_->at(0).x() < t_ - WINDOW_S) {
    freeSeries_->removePoints(0, 1);
  }

  auto* axX1 = qobject_cast<QValueAxis*>(allocChart_->chart()->axisX());
  auto* axY1 = qobject_cast<QValueAxis*>(allocChart_->chart()->axisY());
  if (axX1) axX1->setRange(qMax(0.0, t_ - WINDOW_S), t_);
  if (axY1) {
    double yMax = qMax(axY1->max(), qMax(1.0, s.allocRate * 1.3));
    axY1->setRange(0.0, yMax);
  }

  auto* axX2 = qobject_cast<QValueAxis*>(freeChart_->chart()->axisX());
  auto* axY2 = qobject_cast<QValueAxis*>(freeChart_->chart()->axisY());
  if (axX2) axX2->setRange(qMax(0.0, t_ - WINDOW_S), t_);
  if (axY2) {
    double yMax = qMax(axY2->max(), qMax(1.0, s.freeRate * 1.3));
    axY2->setRange(0.0, yMax);
  }
}
