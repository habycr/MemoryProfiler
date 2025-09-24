#include "SocketServer.h"
#include <iostream>

int main(int argc, char** argv) {
    try {
        std::string ip = "127.0.0.1";
        uint16_t port = 5000;
        if (argc >= 2) ip = argv[1];
        if (argc >= 3) port = static_cast<uint16_t>(std::stoi(argv[2]));

        SocketServer server(ip, port);
        server.start();

        std::cout << "Presiona ENTER para salir..." << std::endl;
        std::string dummy;
        std::getline(std::cin, dummy);

        server.stop();
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Fallo fatal: " << ex.what() << std::endl;
        return 1;
    }
}
