#pragma once
#include "MetricsSnapshot.h"
#include <unordered_map>
#include <vector>
#include <mutex>


// Acumula eventos incrementales y produce un MetricsSnapshot
class MetricsReducer {
public:
    void onAlloc(quint64 ptr, qint64 size, const QString& file, int line, const QString& type, qint64 ts_ns);
    void onFree(quint64 ptr, qint64 ts_ns);
    void setAddressRange(quint64 lo, quint64 hi); // para mapa de memoria
    void setBins(int nBins);
    MetricsSnapshot makeSnapshot(qint64 uptimeMs);
private:
    struct AllocInfo { qint64 size; QString file; int line; QString type; qint64 ts_ns; };
    std::unordered_map<quint64, AllocInfo> live_;
    qint64 heapCurrent_ = 0;
    qint64 heapPeak_ = 0;
    double allocRate_ = 0.0, freeRate_ = 0.0; // EWMA sencilla
    quint64 addrLo_ = 0, addrHi_ = 0;
    int nBins_ = 64;
    qint64 lastTsNsAlloc_ = 0, lastTsNsFree_ = 0; // para tasa
    std::mutex m_;
};