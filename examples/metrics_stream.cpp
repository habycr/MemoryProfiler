#include <iostream>
#include <chrono>
#include <thread>

#include "BrokerClient.h"
#include "MetricsCalculator.h"

int main() {
    BrokerClient client;
    client.configure("127.0.0.1", 5000, "GUI-METRICS");
    if (!client.connect()) {
        std::cerr << "[metrics_stream] No se pudo conectar al broker.\n";
        return 1;
    }
    if (!client.subscribe("MEMORY_UPDATE")) {
        std::cerr << "[metrics_stream] Falló la suscripción a MEMORY_UPDATE.\n";
        return 1;
    }

    MetricsCalculator calc;
    calc.setLeakThresholdMs(30000); // 30s por defecto

    std::cout << "[metrics_stream] Esperando eventos...\n";

    uint64_t count = 0;
    const uint64_t print_every = 50; // imprime cada 50 eventos

    while (client.isConnected()) {
        std::string ev = client.receiveEvent(); // bloqueante
        if (ev.empty()) {
            std::cerr << "[metrics_stream] Conexión cerrada o error recibiendo.\n";
            break;
        }
        calc.processEvent(ev);
        if (++count % print_every == 0) {
            auto snap = calc.getSnapshot();
            std::cout << "[metrics_stream] " << snap.toJSON() << "\n";
        }
    }

    return 0;
}
