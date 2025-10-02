// examples/alloc_b.cpp
#include <vector>
#include <random>
#include <chrono>
#include <thread>
#include <atomic>
#include <string>
#include "memprof_api.h"
struct Obj {
    int a, b, c;
    std::string s;
    Obj(int x, int y) : a(x), b(y), c(x ^ y), s(32, 'x') {}
};

void worker_medium_mixed(std::atomic<bool>& stop) {
    std::mt19937 rng{67890};
    std::uniform_int_distribution<int> midSz(1024, 32 * 1024); // 1KB..32KB
    std::bernoulli_distribution leakProb(0.15);
    std::bernoulli_distribution keepProb(0.6);
    std::vector<void*> liveBufs;
    std::vector<Obj*>  liveObjs;
    liveBufs.reserve(4000);
    liveObjs.reserve(2000);

    using namespace std::chrono_literals;

    while (!stop.load()) {
        // buffers medianos
        for (int i = 0; i < 80; ++i) {
            int n = midSz(rng);
            char* p = new char[n];
            p[0] = 0xEF; p[n-1] = 0xBE; // toque
            if (keepProb(rng)) liveBufs.push_back(p); else delete[] p;
        }

        // objetos con new/delete regulares
        for (int i = 0; i < 40; ++i) {
            Obj* o = new Obj(i, i*2);
            if (leakProb(rng)) {
                liveObjs.push_back(o);   // algunos "leaks" de objetos
            } else {
                delete o;
            }
        }

        // liberar parte de los activos para ver free_rate
        if (!liveBufs.empty()) {
            for (size_t k = 0; k < liveBufs.size() / 8; ++k) {
                size_t idx = k % liveBufs.size();
                delete[] static_cast<char*>(liveBufs[idx]);
                liveBufs[idx] = liveBufs.back();
                liveBufs.pop_back();
            }
        }
        if (!liveObjs.empty()) {
            for (size_t k = 0; k < liveObjs.size() / 5; ++k) {
                size_t idx = k % liveObjs.size();
                delete liveObjs[idx];
                liveObjs[idx] = liveObjs.back();
                liveObjs.pop_back();
            }
        }

        std::this_thread::sleep_for(80ms);
    }

    // cierra casi todo; deja ~10% de leaks medianos/objetos
    size_t keepB = liveBufs.size()/10, keepO = liveObjs.size()/10;
    for (size_t i = keepB; i < liveBufs.size(); ++i) delete[] static_cast<char*>(liveBufs[i]);
    for (size_t i = keepO; i < liveObjs.size(); ++i) delete liveObjs[i];
    // lo restante queda como leak
}
