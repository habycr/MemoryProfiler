#pragma once
#include <QWidget>
#include <QVector>
#include "memprof/proto/MetricsSnapshot.h"

class QTableView;

class MapTab : public QWidget {
    Q_OBJECT
public:
    explicit MapTab(QWidget* parent=nullptr);
    void updateSnapshot(const MetricsSnapshot& s);
    void setLeakThresholdMs(qulonglong ms) { leakThresholdMs_ = ms; } // nuevo
private:
    // Canvas de bins (widget hijo que pinta)
    QWidget* binsCanvas_ = nullptr;

    // Datos
    QVector<BinRange> bins_;
    QVector<LeakItem> blocks_; // bloques vivos

    // Tabla de bloques
    QTableView* table_ = nullptr;

    // Re-render del canvas
    void repaintCanvas();


 // Umbral para marcar LEAK por antigüedad (configurable)
    qulonglong leakThresholdMs_ = 10000; // 10 s por defecto
};
