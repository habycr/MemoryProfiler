// examples/alloc_a.cpp
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <atomic>
#include "memprof_api.h"
// Pequeñas: 16..512 bytes, alta tasa de asignación/liberación.
void worker_small_churn(std::atomic<bool>& stop) {
    std::mt19937 rng{12345};
    std::uniform_int_distribution<int> smallSz(16, 512);
    std::bernoulli_distribution keepProb(0.65); // mantener vivas para ver "activas"
    std::vector<void*> live;
    live.reserve(10000);

    using namespace std::chrono_literals;

    while (!stop.load()) {
        // rachas de 200 asignaciones
        for (int i = 0; i < 200; ++i) {
            const int n = smallSz(rng);
            // usamos char[] para llenar bins pequeños
            char* p = new char[n];
            // toque ligero al bloque para evitar que optimizador lo elimine
            p[0] = 0xAB; p[n-1] = 0xCD;

            if (keepProb(rng)) {
                live.push_back(p);         // queda activo (aparece en mapa)
            } else {
                delete[] p;                // libera (impacta free_rate)
            }
        }

        // churn: libera aprox 15% de los activos
        for (size_t i = 0; i < live.size() / 7; ++i) {
            size_t idx = i % live.size();
            delete[] static_cast<char*>(live[idx]);
            live[idx] = live.back();
            live.pop_back();
        }

        std::this_thread::sleep_for(50ms);
    }

    // Deja ~10% como fugas pequeñas
    size_t keep = live.size() / 10;
    for (size_t i = keep; i < live.size(); ++i) {
        delete[] static_cast<char*>(live[i]);
    }
    // las primeras 'keep' quedan filtradas por la GUI como leaks[]
}
