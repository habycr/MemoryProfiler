// src/tabs/GeneralTab.cpp
#include "GeneralTab.h"
#include "model/MetricsSnapshot.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>

#include <QtCharts/QChart>       // explícito
#include <QtCharts/QChartView>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>

// No uses 'using namespace QtCharts;' aquí.
// En Qt5 ya lo habilita el header con QT_CHARTS_USE_NAMESPACE.

GeneralTab::GeneralTab(QWidget* parent): QWidget(parent) {
  auto* root = new QVBoxLayout(this);
  auto* kpis = new QHBoxLayout(); root->addLayout(kpis);
  heapCur_ = new QLabel("Heap actual: 0 B"); kpis->addWidget(heapCur_);
  heapPeak_ = new QLabel("Pico: 0 B"); kpis->addWidget(heapPeak_);

  // --- Chart alloc ---
  allocSeries_ = new QLineSeries();
  auto* allocChart = new QChart();
  allocChart->legend()->hide();
  allocChart->addSeries(allocSeries_);
  auto* axX1 = new QValueAxis(); axX1->setRange(0, 120);
  allocChart->addAxis(axX1, Qt::AlignBottom); allocSeries_->attachAxis(axX1);
  auto* axY1 = new QValueAxis(); axY1->setTitleText("alloc/s");
  allocChart->addAxis(axY1, Qt::AlignLeft);   allocSeries_->attachAxis(axY1);
  allocChart_ = new QChartView(allocChart); root->addWidget(allocChart_);

  // --- Chart free ---
  freeSeries_ = new QLineSeries();
  auto* freeChart = new QChart();
  freeChart->legend()->hide();
  freeChart->addSeries(freeSeries_);
  auto* axX2 = new QValueAxis(); axX2->setRange(0, 120);
  freeChart->addAxis(axX2, Qt::AlignBottom); freeSeries_->attachAxis(axX2);
  auto* axY2 = new QValueAxis(); axY2->setTitleText("free/s");
  freeChart->addAxis(axY2, Qt::AlignLeft);   freeSeries_->attachAxis(axY2);
  freeChart_ = new QChartView(freeChart); root->addWidget(freeChart_);
}

void GeneralTab::updateSnapshot(const MetricsSnapshot& s) {
  heapCur_->setText(QString("Heap actual: %1 B").arg(s.heapCurrent));
  heapPeak_->setText(QString("Pico: %1 B").arg(s.heapPeak));

  if (allocSeries_->count() > 120) allocSeries_->removePoints(0, allocSeries_->count()-120);
  if (freeSeries_->count()  > 120) freeSeries_->removePoints(0,  freeSeries_->count()-120);

  allocSeries_->append(t_, s.allocRate);
  freeSeries_->append(t_, s.freeRate);
  ++t_;
}
