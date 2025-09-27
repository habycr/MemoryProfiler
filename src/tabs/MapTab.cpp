#include "MapTab.h"
#include <QPainter>
#include <QToolTip>
#include <QMouseEvent>
#include <algorithm>


MapTab::MapTab(QWidget* parent): QWidget(parent) {
    setMouseTracking(true);
}


void MapTab::updateSnapshot(const MetricsSnapshot& s) {
    bins_ = s.bins; update();
}


void MapTab::paintEvent(QPaintEvent*) {
    QPainter p(this);
    p.fillRect(rect(), palette().base());
    if (bins_.isEmpty()) { p.drawText(rect(), Qt::AlignCenter, "Sin datos de bins"); return; }


    // márgenes
    const int L=40, R=20, T=20, B=30;
    QRect area(L, T, width()-L-R, height()-T-B);
    p.drawRect(area);


    qint64 maxBytes = 0; for (const auto& b : bins_) maxBytes = std::max(maxBytes, b.bytes);
    if (maxBytes==0) maxBytes = 1;


    const int n = bins_.size();
    const double w = (double)area.width()/n;


    for (int i=0;i<n;++i) {
        const auto& b = bins_[i];
        double h = (double)b.bytes / (double)maxBytes * area.height();
        QRectF bar(area.left()+i*w+1, area.bottom()-h, w-2, h);
        p.fillRect(bar, QColor(80,140,220));
    }


    // ejes simples
    p.drawText(5, T-4, "bytes");
    p.drawText(area.right()-40, area.bottom()+20, "addr →");
}


void MapTab::mouseMoveEvent(QMouseEvent* e) {
    if (bins_.isEmpty()) return;
    const int L=40, R=20, T=20, B=30; QRect area(L, T, width()-L-R, height()-T-B);
    if (!area.contains(e->pos())) { QToolTip::hideText(); return; }
    const int n = bins_.size();
    double w = (double)area.width()/n; int idx = std::clamp((int)((e->pos().x()-area.left())/w), 0, n-1);
    const auto& b = bins_[idx];
    QString txt = QString("[%1 - %2)\nbytes=%3\nallocs=%4")
    .arg(QString("0x%1").arg(QString::number(b.lo,16)))
    .arg(QString("0x%1").arg(QString::number(b.hi,16)))
    .arg((qlonglong)b.bytes).arg(b.allocations);
    QToolTip::showText(e->globalPos(), txt, this);
}