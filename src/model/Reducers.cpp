#include "Reducers.h"
#include <algorithm>


void MetricsReducer::onAlloc(quint64 ptr, qint64 size, const QString& file, int line, const QString& type, qint64 ts_ns) {
std::scoped_lock lk(m_);
live_[ptr] = AllocInfo{size, file, line, type, ts_ns};
heapCurrent_ += size;
heapPeak_ = std::max(heapPeak_, heapCurrent_);
if (lastTsNsAlloc_ > 0) {
double dt = (ts_ns - lastTsNsAlloc_) / 1e9; if (dt > 0) { double inst = 1.0 / dt; allocRate_ = 0.9*allocRate_ + 0.1*inst; }
}
lastTsNsAlloc_ = ts_ns;
}


void MetricsReducer::onFree(quint64 ptr, qint64 ts_ns) {
std::scoped_lock lk(m_);
auto it = live_.find(ptr);
if (it != live_.end()) {
heapCurrent_ -= it->second.size;
live_.erase(it);
}
if (lastTsNsFree_ > 0) {
double dt = (ts_ns - lastTsNsFree_) / 1e9; if (dt > 0) { double inst = 1.0 / dt; freeRate_ = 0.9*freeRate_ + 0.1*inst; }
}
lastTsNsFree_ = ts_ns;
}


void MetricsReducer::setAddressRange(quint64 lo, quint64 hi) { std::scoped_lock lk(m_); addrLo_ = lo; addrHi_ = hi; }
void MetricsReducer::setBins(int nBins) { std::scoped_lock lk(m_); nBins_ = std::max(4, nBins); }


MetricsSnapshot MetricsReducer::makeSnapshot(qint64 uptimeMs) {
std::scoped_lock lk(m_);
MetricsSnapshot s; s.heapCurrent = heapCurrent_; s.heapPeak = heapPeak_;
s.allocRate = allocRate_; s.freeRate = freeRate_; s.uptimeMs = uptimeMs; s.capturedAt = QDateTime::currentDateTime();
// Construir bins (distribución por rango de direcciones)
s.bins.clear(); s.bins.reserve(nBins_);
if (addrHi_ > addrLo_ && nBins_ > 0) {
const long double span = (long double)addrHi_ - (long double)addrLo_;
for (int i=0;i<nBins_;++i){
quint64 lo = addrLo_ + (quint64)((span * i)/nBins_);
quint64 hi = addrLo_ + (quint64)((span * (i+1))/nBins_);
s.bins.push_back({lo,hi,0,0});
}
for (const auto& kv : live_) {
quint64 p = kv.first; const auto& info = kv.second;
if (p < addrLo_ || p >= addrHi_) continue;
int idx = (int)(((long double)(p - addrLo_) / (long double)(addrHi_ - addrLo_)) * nBins_);
if (idx < 0) idx = 0; if (idx >= nBins_) idx = nBins_-1;
s.bins[idx].bytes += info.size; s.bins[idx].allocations += 1;
}
}
// Por archivo y leaks
QHash<QString, FileStat> acc;
for (const auto& kv : live_) {
const auto& a = kv.second;
auto &fs = acc[a.file]; fs.file = a.file; fs.totalBytes += a.size; fs.allocs += 1; fs.netBytes += a.size;
}
s.perFile.reserve(acc.size());
for (auto it = acc.begin(); it != acc.end(); ++it) s.perFile.push_back(it.value());
s.leaks.reserve(live_.size());
for (const auto& kv : live_) {
const auto& a = kv.second;
s.leaks.push_back({kv.first, a.size, a.file, a.line, a.type, a.ts_ns});
}
return s;
}