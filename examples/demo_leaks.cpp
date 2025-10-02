// examples/demo_leaks.cpp
#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>
#include <atomic>
#include "memprof_api.h"

// ---- Declaraciones forward: vienen de alloc_a.cpp y alloc_b.cpp ----
void worker_small_churn(std::atomic<bool>& stop);   // alloc_a.cpp
void worker_medium_mixed(std::atomic<bool>& stop);  // alloc_b.cpp

int main() {
    // Conecta al GUI (servidor) en 127.0.0.1:7070
    memprof_init("127.0.0.1", 7070);
    std::puts("[demo] arrancó; generando asignaciones y algunas fugas…");

    // Lanza dos workers en TUs distintos para asegurar múltiples archivos
    std::atomic<bool> stop{false};
    std::thread tA([&]{ worker_small_churn(stop); });   // -> fugas atribuibles a alloc_a.cpp
    std::thread tB([&]{ worker_medium_mixed(stop); });  // -> fugas atribuibles a alloc_b.cpp

    // Además, fugas locales de este TU (demo_leaks.cpp) para una 3ra categoría
    std::vector<char*> keep; keep.reserve(200);
    for (int i = 0; i < 200; ++i) {
        size_t sz = 1024 + (i % 64) * 1024;       // 1–65 KiB
        char* p = new char[sz];
        if (i % 3 == 0) keep.push_back(p); else delete[] p;
        if (i % 20 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // Espera suficiente para que el umbral de fugas (3000 ms) las marque como "leak"
    std::puts("[demo] esperando 4s para clasificación de fugas…");
    std::this_thread::sleep_for(std::chrono::seconds(4));

    // Libera una parte de las locales
    for (size_t i = 0; i < keep.size(); i += 5) {
        delete[] keep[i]; keep[i] = nullptr;
    }

    std::puts("[demo] corriendo 5s más enviando snapshots…");
    std::this_thread::sleep_for(std::chrono::seconds(5));

    // Detener workers (dejan ~10% como leaks según su propia lógica)
    stop.store(true);
    tA.join();
    tB.join();

    memprof_shutdown();
    std::puts("[demo] fin.");
    return 0;
}
