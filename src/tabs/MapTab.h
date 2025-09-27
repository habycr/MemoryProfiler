#pragma once
#include <QWidget>
#include "model/MetricsSnapshot.h"


class MapTab : public QWidget {
    Q_OBJECT
    public:
    explicit MapTab(QWidget* parent=nullptr);
    void updateSnapshot(const MetricsSnapshot& s);
protected:
    void paintEvent(QPaintEvent*) override;
    void mouseMoveEvent(QMouseEvent* e) override;
private:
    QVector<BinRange> bins_;
    QString hoverText_;
};