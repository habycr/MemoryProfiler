#include "GeneralTab.h"
#include "../../../include/memprof/proto/MetricsSnapshot.h"

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
