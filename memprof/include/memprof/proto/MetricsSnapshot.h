#pragma once

// ===== Soporte dual (backend std / frontend Qt) =====
// El backend define MEMPROF_NO_QT en CMake.
// En backend, proveemos "shims" con los NOMBRES de tipos de Qt (QString, QVector,
// qulonglong, qlonglong) pero mapeados a std::* para no romper APIs ni nombres de campos.
#ifdef MEMPROF_NO_QT
  #include <cstdint>
  #include <string>
  #include <vector>
  using qulonglong = std::uint64_t;
  using qlonglong  = std::int64_t;
  using QString    = std::string;
  template <typename T> using QVector = std::vector<T>;
#else
  #include <QtGlobal>
  #include <QString>
  #include <QVector>
#endif

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
    qulonglong ts_ns = 0; // timestamp de asignación (steady)
    bool       isLeak = false; // decidido en el runtime/backend
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
    qlonglong  topLeakBytes  = 0;

    // Secciones
    QVector<BinRange>  bins;
    QVector<FileStat>  perFile;
    QVector<LeakItem>  leaks;
};
