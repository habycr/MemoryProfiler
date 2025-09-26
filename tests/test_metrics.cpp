#include <cassert>
#include <iostream>
#include <thread>    // para std::this_thread
#include <chrono>    // para std::chrono::milliseconds
#include "MetricsCalculator.h"


int main() {
    MetricsCalculator calc;
    calc.setLeakThresholdMs(1); // umbral muy bajo para facilitar pruebas de leak

    // Simular dos ALLOC y un FREE
    calc.processEvent(R"({
        "kind":"ALLOC","ptr":"0x123","size":1024,
        "file":"a.cpp","line":10,"type":"int","is_array":false,
        "ts_ns": 1000000, "thread": 1
    })");
    calc.processEvent(R"({
        "kind":"ALLOC","ptr":"0x456","size":2048,
        "file":"b.cpp","line":20,"type":"char[]","is_array":true,
        "ts_ns": 2000000, "thread": 2
    })");
    calc.processEvent(R"({
        "kind":"FREE","ptr":"0x123","size":1024,
        "file":"a.cpp","line":10,"type":"int","is_array":false,
        "ts_ns": 3000000, "thread": 1
    })");

    auto snap = calc.getSnapshot();
    std::cout << "Snapshot: " << snap.toJSON() << "\n";

    // Comprobaciones (tolerancia por representación en MB)
    // Vivo: solo 0x456 (2048 bytes)
    assert(snap.active_allocs == 1);
    assert(snap.total_allocs  == 2);

    // current_mb ≈ 2048 / (1024*1024) ≈ 0.00195 MB
    const double expected_cur_mb = 2048.0 / (1024.0 * 1024.0);
    assert(std::abs(snap.current_mb - expected_cur_mb) < 1e-6);

    // peak al menos el total tras el segundo ALLOC: (1024 + 2048) bytes
    const double expected_peak_mb = (1024.0 + 2048.0) / (1024.0 * 1024.0);
    assert(snap.peak_mb + 1e-6 >= expected_peak_mb);

    // Espera un poquito para que supere el umbral de leak y vuelva a tomar snapshot
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    auto snap2 = calc.getSnapshot();
    // leak_mb debería contar 0x456 si ya pasó el umbral
    assert(snap2.leak_mb >= expected_cur_mb - 1e-6);

    std::cout << "OK\n";
    return 0;
}
