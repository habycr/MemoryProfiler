#include "Reducers.h"
#include <QHash>
#include <QString>
#include <algorithm>

void MetricsReducer::onAlloc(quint64 ptr, qint64 size, const QString& file, int line, const QString& type, qint64 ts_ns) {
    std::scoped_lock lk(m_);
    live_[ptr] = AllocInfo{ size, file, line, type, ts_ns };

    heapCurrent_ += size;
    heapPeak_     = std::max(heapPeak_, heapCurrent_);

    if (lastTsNsAlloc_ > 0) {
        const double dt = (ts_ns - lastTsNsAlloc_) / 1e9;
        if (dt > 0.0) {
            const double inst = 1.0 / dt;
            allocRate_ = 0.9 * allocRate_ + 0.1 * inst;
        }
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
        const double dt = (ts_ns - lastTsNsFree_) / 1e9;
        if (dt > 0.0) {
            const double inst = 1.0 / dt;
            freeRate_ = 0.9 * freeRate_ + 0.1 * inst;
        }
    }
    lastTsNsFree_ = ts_ns;
}

void MetricsReducer::setAddressRange(quint64 lo, quint64 hi) {
    std::scoped_lock lk(m_);
    addrLo_ = lo;
    addrHi_ = hi;
}

void MetricsReducer::setBins(int nBins) {
    std::scoped_lock lk(m_);
    nBins_ = std::max(4, nBins);
}

MetricsSnapshot MetricsReducer::makeSnapshot(qint64 uptimeMs) {
    std::scoped_lock lk(m_);

    MetricsSnapshot s;
    s.heapCurrent = heapCurrent_;
    s.heapPeak    = heapPeak_;
    s.allocRate   = allocRate_;
    s.freeRate    = freeRate_;
    s.uptimeMs    = uptimeMs; // la GUI puede tomar su propio QDateTime si lo necesita

    // --- Bins (mapa de memoria) ---
    s.bins.clear();
    s.bins.reserve(static_cast<size_t>(nBins_));

    if (addrHi_ > addrLo_ && nBins_ > 0) {
        const long double span = static_cast<long double>(addrHi_) - static_cast<long double>(addrLo_);

        for (int i = 0; i < nBins_; ++i) {
            const quint64 lo = addrLo_ + static_cast<quint64>((span * i) / nBins_);
            const quint64 hi = addrLo_ + static_cast<quint64>((span * (i + 1)) / nBins_);
            s.bins.push_back({ lo, hi, 0, 0 });
        }

        for (const auto& kv : live_) {
            const quint64 p    = kv.first;
            const auto&   info = kv.second;
            if (p < addrLo_ || p >= addrHi_) continue;

            int idx = static_cast<int>(
                (static_cast<long double>(p - addrLo_) / static_cast<long double>(addrHi_ - addrLo_)) * nBins_
            );
            if (idx < 0)       idx = 0;
            if (idx >= nBins_) idx = nBins_ - 1;

            s.bins[static_cast<size_t>(idx)].bytes       += info.size;
            s.bins[static_cast<size_t>(idx)].allocations += 1;
        }
    }

    // --- Por archivo (acumulador interno) ---
    QHash<QString, PerFileAcc> acc;
    for (const auto& kv : live_) {
        const auto& a = kv.second;
        auto& fs = acc[a.file];
        fs.file       = a.file;
        fs.totalBytes += a.size;
        fs.allocs     += 1;
        fs.netBytes   += a.size;
    }

    // Convertir PerFileAcc -> DTO FileStat (del MetricsSnapshot.h)
    s.perFile.reserve(static_cast<size_t>(acc.size()));
    for (auto it = acc.cbegin(); it != acc.cend(); ++it) {
        FileStat dto;             // FileStat del DTO compartido
        dto.file       = it->file;
        dto.totalBytes = it->totalBytes;
        dto.allocs     = it->allocs;
        dto.netBytes   = it->netBytes;
        // dto.frees queda en 0 aquí (si luego lo calculas, asígnalo)
        s.perFile.push_back(dto);
    }

    // --- Leaks (bloques vivos) ---
    s.leaks.reserve(live_.size());
    for (const auto& kv : live_) {
        const auto& a = kv.second;

        LeakItem li;              // LeakItem del DTO: {ptr,size,file,line,type,ts_ns}
        li.ptr   = kv.first;
        li.size  = a.size;
        li.file  = a.file;
        li.line  = a.line;
        li.type  = a.type;
        li.ts_ns = static_cast<qulonglong>(a.ts_ns);

        s.leaks.push_back(li);
    }

    return s;
}
