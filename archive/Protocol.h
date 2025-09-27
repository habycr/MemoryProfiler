#pragma once
#include <string>
#include <vector>
#include <deque>
#include <unordered_map>
#include <optional>

namespace protocol {

    // Percent-encode solo los caracteres conflictivos con el pipe-protocol.
    std::string encode(const std::string& in);
    std::string decode(const std::string& in);

    // Divide línea de protocolo en tokens (maneja %xx).
    std::vector<std::string> split(const std::string& line);

    // Estructura de petición genérica: COMANDO + args.
    struct Request {
        std::string command;
        std::vector<std::string> args;
    };

    std::optional<Request> parseLine(const std::string& line);

    // Helpers de respuesta
    inline std::string ok(const std::string& msg = {}) {
        return msg.empty() ? "OK\n" : "OK|" + encode(msg) + "\n";
    }
    inline std::string error(const std::string& msg) {
        return "ERROR|" + encode(msg) + "\n";
    }

    // Tópicos del profiler
    static const char* TOPIC_MEMORY_UPDATE = "MEMORY_UPDATE";
    static const char* TOPIC_ALLOCATION    = "ALLOCATION";
    static const char* TOPIC_DEALLOCATION  = "DEALLOCATION";
    static const char* TOPIC_LEAK_DETECTED = "LEAK_DETECTED";
    static const char* TOPIC_FILE_STATS    = "FILE_STATS";

} // namespace protocol
