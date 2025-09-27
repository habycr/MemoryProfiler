#include <vector>
#include <thread>
#include <chrono>
#include <cstdio>
#include "memprof_api.h"

int main() {
    // Conecta al GUI (servidor) en 127.0.0.1:7070
    memprof_init("127.0.0.1", 7070);

    std::puts("[demo] arrancó; generando asignaciones y algunas fugas…");

    std::vector<char*> keep;   // punteros que se "filtrarán"
    keep.reserve(200);

    // 1) Hacemos ráfagas de alloc/free y dejamos algunos sin liberar
    for (int i = 0; i < 200; ++i) {
        size_t sz = 1024 + (i % 64) * 1024;       // 1–65 KiB
        char* p = new char[sz];
        if (i % 3 == 0) {
            keep.push_back(p);                    // fuga intencional
        } else {
            delete[] p;                           // libre normal
        }
        if (i % 20 == 0) std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }

    // 2) Espera > 3s para que el umbral de fugas (3000 ms) las marque como leaks
    std::puts("[demo] esperando 4s para que el GUI clasifique fugas…");
    std::this_thread::sleep_for(std::chrono::seconds(4));

    // 3) Algunas liberaciones tardías (para que se vea movimiento)
    for (size_t i = 0; i < keep.size(); i += 5) {
        delete[] keep[i];                          // liberamos una parte
        keep[i] = nullptr;
    }

    std::puts("[demo] corriendo 5s más enviando snapshots…");
    std::this_thread::sleep_for(std::chrono::seconds(50));

    // Nota: no liberamos el resto a propósito (quedan como leaks)
    memprof_shutdown();
    std::puts("[demo] fin.");
    return 0;
}
