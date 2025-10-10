#pragma once

#include "memprof/proto/MetricsSnapshot.h"  // DTOs: MetricsSnapshot, FileStat (DTO), LeakItem (DTO)
#include <unordered_map>
#include <mutex>
#include <QtGlobal>   // qint64, quint64
#include <QString>

// Acumulador interno (evita chocar con el FileStat del DTO)
struct PerFileAcc {
    QString file;
    qint64  totalBytes = 0;
    int     allocs     = 0;
    qint64  netBytes   = 0;
};

class MetricsReducer {
public:
    void onAlloc(quint64 ptr, qint64 size, const QString& file, int line, const QString& type, qint64 ts_ns);
    void onFree (quint64 ptr, qint64 ts_ns);

    void setAddressRange(quint64 lo, quint64 hi); // para mapa de memoria
    void setBins(int nBins);

    MetricsSnapshot makeSnapshot(qint64 uptimeMs);

private:
    struct AllocInfo {
        qint64   size;
        QString  file;
        int      line;
        QString  type;
        qint64   ts_ns;
    };

    std::unordered_map<quint64, AllocInfo> live_;

    qint64 heapCurrent_ = 0;
    qint64 heapPeak_    = 0;
    double allocRate_   = 0.0;
    double freeRate_    = 0.0;   // EWMA

    quint64 addrLo_ = 0, addrHi_ = 0;
    int     nBins_  = 64;

    qint64 lastTsNsAlloc_ = 0, lastTsNsFree_ = 0;

    std::mutex m_;
};
