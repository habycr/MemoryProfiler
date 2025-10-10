// examples/alloc_c.cpp
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <atomic>
#include "../include/memprof/core/Runtime.h"
void worker_large_spikes(std::atomic<bool>& stop) {
    using namespace std::chrono_literals;
    std::mt19937 rng{13579};
    std::uniform_int_distribution<int> bigSz(64 * 1024, 2 * 1024 * 1024); // 64KB..2MB
    std::vector<void*> bigLive;
    bigLive.reserve(512);

    while (!stop.load()) {
        // Pico de bloques grandes
        for (int i = 0; i < 24; ++i) {
            int n = bigSz(rng);
            char* p = new char[n];
            p[0] = 0x55; p[n-1] = 0xAA;
            bigLive.push_back(p);
        }
        std::this_thread::sleep_for(300ms);

        // Libera la mayoría (deja algunos vivos para "heap_peak" y bins altos)
        for (size_t i = 0; i < bigLive.size(); ++i) {
            // deja ~20% sin liberar esta vuelta
            if (i % 5 != 0) {
                delete[] static_cast<char*>(bigLive[i]);
                bigLive[i] = nullptr;
            }
        }
        // compacta vector y deja algunos vivos (acumulando leaks grandes)
        std::vector<void*> tmp;
        tmp.reserve(bigLive.size());
        for (void* p : bigLive) if (p) tmp.push_back(p);
        bigLive.swap(tmp);

        // breve pausa para que la GUI vea el pico
        std::this_thread::sleep_for(300ms);
    }

    // Deja ~15% como fugas grandes
    size_t keep = bigLive.size() * 15 / 100;
    for (size_t i = keep; i < bigLive.size(); ++i)
        delete[] static_cast<char*>(bigLive[i]);
    // lo demás queda como leak grande (aporta a leak_bytes)
}
