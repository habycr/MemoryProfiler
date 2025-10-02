#include "GeneralTab.h"
#include "model/MetricsSnapshot.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QTableWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QtMath>
#include <algorithm>

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

  // ================= Memoria vs tiempo (MB) =================
  // ÚNICA serie y sin OpenGL para evitar artefactos de doble trazo
  memSeries_ = new QLineSeries();
  // memSeries_->setUseOpenGL(true); // <- DESACTIVADO a propósito

  memChart_ = new QChart();
  memChart_->legend()->hide();
  memChart_->addSeries(memSeries_);

  axX_mem_ = new QValueAxis();
  axX_mem_->setTitleText("t (s)");
  axX_mem_->setRange(0.0, 60.0);

  axY_mem_ = new QValueAxis();
  axY_mem_->setTitleText("MB");
  axY_mem_->setRange(0.0, 1.0);

  memChart_->addAxis(axX_mem_, Qt::AlignBottom);
  memChart_->addAxis(axY_mem_, Qt::AlignLeft);
  memSeries_->attachAxis(axX_mem_);
  memSeries_->attachAxis(axY_mem_);

  memChartView_ = new QChartView(memChart_);
  memChartView_->setRenderHint(QPainter::Antialiasing);
  root->addWidget(memChartView_);

  // ================= Chart de allocs/s =================
  allocSeries_ = new QLineSeries();
  // allocSeries_->setUseOpenGL(true); // opcional; si ves “doble línea”, déjalo OFF
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

  // ================= Chart de frees/s =================
  freeSeries_ = new QLineSeries();
  // freeSeries_->setUseOpenGL(true); // opcional; si ves “doble línea”, déjalo OFF
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

  // ----- Top-3 por archivo -----
  top3_ = new QTableWidget(3, 3, this);
  QStringList headers; headers << "Archivo" << "Allocs" << "MB";
  top3_->setHorizontalHeaderLabels(headers);
  top3_->verticalHeader()->setVisible(false);
  top3_->horizontalHeader()->setStretchLastSection(true);
  top3_->setEditTriggers(QAbstractItemView::NoEditTriggers);
  top3_->setSelectionMode(QAbstractItemView::NoSelection);
  root->addWidget(top3_);
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
  double nextX = (s.uptimeMs > 0) ? (s.uptimeMs / 1000.0) : (t_ + 0.25);
  if (nextX <= t_) nextX = t_ + 0.25;
  t_ = nextX;

  const double memMB = s.heapCurrent / (1024.0 * 1024.0);
  memSeries_->append(t_, memMB);

  // Ventana ~120 s
  const double WINDOW_S = 120.0;
  while (memSeries_->count() > 0 && memSeries_->at(0).x() < t_ - WINDOW_S) {
    memSeries_->removePoints(0, 1);
  }

  // Actualizar ejes usando punteros persistentes (sin buscar en el chart)
  axX_mem_->setRange(std::max(0.0, t_ - WINDOW_S), t_);
  axY_mem_->setRange(0.0, std::max(axY_mem_->max(), std::max(1.0, memMB * 1.2)));

  // ----- Series de allocs/s y frees/s -----
  allocSeries_->append(t_, s.allocRate);
  freeSeries_->append(t_, s.freeRate);

  while (allocSeries_->count() > 0 && allocSeries_->at(0).x() < t_ - WINDOW_S)
    allocSeries_->removePoints(0, 1);
  while (freeSeries_->count() > 0 && freeSeries_->at(0).x() < t_ - WINDOW_S)
    freeSeries_->removePoints(0, 1);

  auto* axX1 = qobject_cast<QValueAxis*>(allocChart_->chart()->axisX());
  auto* axY1 = qobject_cast<QValueAxis*>(allocChart_->chart()->axisY());
  if (axX1) axX1->setRange(std::max(0.0, t_ - WINDOW_S), t_);
  if (axY1) axY1->setRange(0.0, std::max(axY1->max(), std::max(1.0, s.allocRate * 1.3)));

  auto* axX2 = qobject_cast<QValueAxis*>(freeChart_->chart()->axisX());
  auto* axY2 = qobject_cast<QValueAxis*>(freeChart_->chart()->axisY());
  if (axX2) axX2->setRange(std::max(0.0, t_ - WINDOW_S), t_);
  if (axY2) axY2->setRange(0.0, std::max(axY2->max(), std::max(1.0, s.freeRate * 1.3)));

  // ----- Top-3 por archivo -----
  struct R { QString file; int allocs; qint64 bytes; };
  std::vector<R> rows; rows.reserve(s.perFile.size());
  for (const auto& f : s.perFile) rows.push_back({f.file, f.allocs, f.netBytes});
  std::sort(rows.begin(), rows.end(), [](const R& a, const R& b){
    if (a.bytes != b.bytes) return a.bytes > b.bytes;
    return a.allocs > b.allocs;
  });
  const int N = std::min<int>(3, (int)rows.size());
  top3_->clearContents();
  top3_->setRowCount(N);
  for (int i=0;i<N;++i) {
    top3_->setItem(i, 0, new QTableWidgetItem(rows[i].file));
    top3_->setItem(i, 1, new QTableWidgetItem(QString::number(rows[i].allocs)));
    top3_->setItem(i, 2, new QTableWidgetItem(bytesToHuman(rows[i].bytes)));
  }
}
