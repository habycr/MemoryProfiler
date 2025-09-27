// examples/demo_stress.cpp
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <random>
#include <string>
#include <thread>
#include <vector>

// IMPORTANTE: activar el wrapper y luego incluir la API DESPUÉS de la STL
#define MEMPROF_WRAP_NEW 1
#include "memprof_api.h"   // heredado desde el target 'memprof' (include dirs PUBLIC)

static void sleep_ms(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

int main() {
    std::puts("[demo_stress] start");
    // Conéctate a la GUI (ServerWorker escucha por defecto en 7070, Any/localhost)
    memprof_init("127.0.0.1", 7070);

    std::mt19937_64 rng{1234567};
    std::uniform_int_distribution<int> alloc_size(1 << 10, 1 << 16); // 1KB..64KB
    std::bernoulli_distribution       leak_prob(0.15);               // 15% quedan sin liberar
    std::uniform_int_distribution<int> hold_ms(20, 200);             // tiempo de vida de bloques

    struct Block { char* p; size_t n; int ttl; };
    std::vector<Block> live;

    const auto t0 = std::chrono::steady_clock::now();
    while (std::chrono::steady_clock::now() - t0 < std::chrono::seconds(20)) {
        // lote de asignaciones
        for (int i = 0; i < 200; ++i) {
            const size_t n = static_cast<size_t>(alloc_size(rng));
            char* p = new char[n];                 // envuelto por MEMPROF_WRAP_NEW
            if (leak_prob(rng)) {
                live.push_back({p, n, -1});        // -1 == leak (no se libera nunca)
            } else {
                live.push_back({p, n, hold_ms(rng)});
            }
        }

        // reducir TTL y liberar algunas
        for (auto& b : live) {
            if (b.ttl > 0) {
                b.ttl -= 10;
                if (b.ttl <= 0) {
                    delete[] b.p;                  // también envuelto
                    b.p = nullptr;
                    b.ttl = 0;
                }
            }
        }

        // compactar los ya liberados
        live.erase(std::remove_if(live.begin(), live.end(),
                   [](const Block& b){ return b.ttl == 0 && b.p == nullptr; }),
                   live.end());

        std::puts("[demo_stress] tick");
        sleep_ms(10);
    }

    std::puts("[demo_stress] waiting 4s for leak classification...");
    sleep_ms(4000);

    std::puts("[demo_stress] done.");
    memprof_shutdown();
    return 0;
}
