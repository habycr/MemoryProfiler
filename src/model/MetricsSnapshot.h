// src/model/MetricsSnapshot.h
#pragma once
#include <QString>
#include <QVector>
#include <QDateTime>

struct FileStat { QString file; qint64 totalBytes=0; int allocs=0; int frees=0; qint64 netBytes=0; };

struct LeakItem { quint64 ptr=0; qint64 size=0; QString file; int line=0; QString type; qint64 ts_ns=0; };

struct BinRange { quint64 lo=0, hi=0; qint64 bytes=0; int allocations=0; };

struct MetricsSnapshot {
    qint64 heapCurrent=0, heapPeak=0;
    double allocRate=0.0, freeRate=0.0;
    qint64 uptimeMs=0;

    QVector<BinRange> bins;
    QVector<FileStat> perFile;
    QVector<LeakItem> leaks;

    QDateTime capturedAt;
};
