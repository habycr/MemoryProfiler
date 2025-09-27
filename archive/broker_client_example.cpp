#include <iostream>
#include "BrokerClient.h"

int main() {
    BrokerClient client;
    client.configure("127.0.0.1", 5000, "TEST-GUI");

    if (!client.connect()) {
        std::cerr << "Error conectando al broker\n";
        return 1;
    }
    if (!client.subscribe("MEMORY_UPDATE")) {
        std::cerr << "Error suscribiéndose\n";
        return 1;
    }

    std::cout << "Esperando 10 eventos...\n";
    for (int i = 0; i < 10; ++i) {
        std::string json = client.receiveEvent();
        if (json.empty()) {
            std::cerr << "Conexión cerrada o error al recibir.\n";
            break;
        }
        std::cout << "Evento " << i << ": " << json << "\n";
    }

    client.disconnect();
    return 0;
}
