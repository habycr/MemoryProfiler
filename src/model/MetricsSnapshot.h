#pragma once
#include <QtGlobal>
#include <QString>
#include <QVector>

// --- Rangos (para bins del Mapa de Memoria) ---
struct BinRange {
    qulonglong lo = 0;
    qulonglong hi = 0;
    qlonglong  bytes = 0;
    int        allocations = 0;
};

// --- Estadísticas por archivo ---
struct FileStat {
    QString   file;
    qlonglong totalBytes = 0;  // bytes acumulados en allocs
    int       allocs = 0;      // cantidad de allocs
    int       frees  = 0;      // cantidad de frees (estimada si no llega)
    qlonglong netBytes = 0;    // bytes vivos (live)
};

// --- Item de bloque vivo (posibles fugas) ---
struct LeakItem {
    qulonglong ptr = 0;  // dirección (entero)
    qlonglong  size = 0;
    QString    file;
    int        line = 0;
    QString    type;
    qulonglong ts_ns = 0; // timestamp de asignación
};

// --- Snapshot que consume la GUI ---
struct MetricsSnapshot {
    // General
    qulonglong heapCurrent = 0;
    qulonglong heapPeak    = 0;
    qulonglong activeAllocs= 0;
    qulonglong totalAllocs = 0;
    qulonglong leakBytes   = 0;
    double     allocRate   = 0.0;   // alloc/s (runtime)
    double     freeRate    = 0.0;   // free/s  (runtime)
    qulonglong uptimeMs    = 0;

    // KPIs de fugas (runtime)
    double     leakRate      = 0.0;       // leaks / total allocs
    qulonglong largestLeakSz = 0;
    QString    largestLeakFile;
    QString    topLeakFile;
    int        topLeakCount  = 0;
    qulonglong topLeakBytes  = 0;

    // Secciones
    QVector<BinRange>  bins;
    QVector<FileStat>  perFile;
    QVector<LeakItem>  leaks;
};
