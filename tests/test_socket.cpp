// tests/test_socket.cpp
// Cliente de pruebas "pro" para el broker: SUBSCRIBE / PUBLISH / LEGACY-BRIDGE

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <csignal>
#include <cstdio>   // std::snprintf

#include "Protocol.h"
#include "MemoryTracker.h"  // ProfilerClient (SDK)
#include "NetUtils.h"
#include "LegacyBridge.h"   // instalar sink legacy -> broker

// ====== Utilidades de consola (UTF-8 en Windows) ======
#ifdef _WIN32
  #define WIN32_LEAN_AND_MEAN
  #define NOMINMAX
  #include <windows.h>
  static void setup_utf8_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
  }
#else
  static void setup_utf8_console() {}
#endif

// Sentinel de socket inválido portable
#ifdef _WIN32
  constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
  constexpr socket_t INVALID_SOCK = (socket_t)-1;
#endif

// ====== Señal de salida segura ======
static std::atomic<bool> g_running{true};
static void on_sigint(int) { g_running = false; }

// ====== Conexión simple ======
static socket_t connect_to(const std::string& ip, uint16_t port) {
  socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (IS_INVALID(s)) return INVALID_SOCK;

  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
  if (::inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) {
    CLOSESOCK(s); return INVALID_SOCK;
  }
  if (::connect(s, (sockaddr*)&a, sizeof(a)) < 0) {
    CLOSESOCK(s); return INVALID_SOCK;
  }
  return s;
}

static bool send_line(socket_t s, const std::string& line) {
  if (!net::sendAll(s, line.c_str(), line.size())) {
  #ifdef _WIN32
    std::cerr << "[send] WSAGetLastError=" << WSAGetLastError() << "\n";
  #endif
    return false;
  }
  return true;
}

// ====== SUBSCRIBE (y lector asíncrono) ======
static bool subscribe_and_readloop(const std::string& ip, uint16_t port,
                                   const std::string& topic,
                                   const std::string& appId)
{
  socket_t s = connect_to(ip, port);
  if (IS_INVALID(s)) { std::cerr << "[sub] no se pudo conectar\n"; return false; }

  std::string line = std::string("SUBSCRIBE|")
                   + protocol::encode(topic) + "|"
                   + protocol::encode(appId) + "\n";
  if (!send_line(s, line)) {
    std::cerr << "[sub] fallo send\n"; CLOSESOCK(s); return false;
  }

  std::string resp;
  if (!net::recvLine(s, resp)) {
    std::cerr << "[sub] sin respuesta\n"; CLOSESOCK(s); return false;
  }
  std::cout << "[sub] resp: " << resp << "\n";
  if (resp.rfind("OK", 0) != 0) {
    CLOSESOCK(s); return false;
  }

  // lector en background
  std::thread reader([s](){
    std::string r;
    while (g_running && net::recvLine(s, r)) {
      std::cout << "[RX] " << r << std::endl;
      r.clear();
    }
    CLOSESOCK(s);
  });
  reader.detach();
  return true;
}

// ====== PUBLISH por SDK (ProfilerClient) ======
static bool publish_once_sdk(const std::string& topic, const std::string& payload) {
  try {
    ProfilerClient::instance().publish(topic, payload);
    return true;
  } catch (const std::exception& e) {
    std::cerr << "[pub] error: " << e.what() << "\n";
    return false;
  }
}

// ====== PUBLISH raw (opcional) ======
static bool publish_once_raw(const std::string& ip, uint16_t port,
                             const std::string& topic, const std::string& payload)
{
  socket_t s = connect_to(ip, port);
  if (IS_INVALID(s)) { std::cerr << "[pub] no se pudo conectar\n"; return false; }
  std::string line = std::string("PUBLISH|")
                   + protocol::encode(topic) + "|"
                   + protocol::encode(payload) + "\n";
  bool ok = send_line(s, line);
  CLOSESOCK(s);
  return ok;
}

// ====== Generar JSON de ejemplo ======
static std::string make_sample_json(const char* kind, std::size_t bytes, int iter) {
  // Campos típicos del legacy
  char buf[512];
  auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
  auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();
  std::snprintf(buf, sizeof(buf),
    "{\"kind\":\"%s\",\"ptr\":\"0x%08X\",\"size\":%zu,"
    "\"file\":\"test_socket.cpp\",\"line\":%d,"
    "\"type\":\"int[]\",\"is_array\":1,"
    "\"ts_ns\":%lld,\"thread\":%lld}",
    kind, 0xDEAD0000 + iter, bytes, 123 + iter,
    (long long)ns, (long long)std::hash<std::thread::id>{}(std::this_thread::get_id()));
  return std::string(buf);
}

// ====== Generar eventos reales con legacy (new/delete) ======
static void generate_legacy_events(int rounds, int blocks, int block_elems) {
  for (int r = 0; r < rounds && g_running; ++r) {
    std::vector<int*> v; v.reserve(blocks);
    for (int i = 0; i < blocks; ++i) v.push_back(new int[block_elems]);
    for (int* p : v) delete[] p;
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
  }
}

// ====== CLI ======
static void usage(const char* prog) {
  std::cout <<
    "Uso:\n"
    "  " << prog << " [opciones]\n\n"
    "Opciones principales (puedes combinar --sub y --bridge):\n"
    "  --sub                 Suscribirse y mostrar mensajes\n"
    "  --pub                 Publicar 1 mensaje de ejemplo por SDK\n"
    "  --pubraw              Publicar 1 mensaje de ejemplo por socket crudo\n"
    "  --bridge              Instalar puente legacy y generar new/delete\n"
    "\nOpciones comunes:\n"
    "  --host=127.0.0.1      Host del broker (defecto 127.0.0.1)\n"
    "  --port=5000           Puerto del broker (defecto 5000)\n"
    "  --topic=MEMORY_UPDATE Topico (defecto MEMORY_UPDATE)\n"
    "  --app=APP-1           appId para SDK/bridge (defecto APP-1)\n"
    "\nOpciones de carga:\n"
    "  --rounds=5            Iteraciones de alloc/free (bridge)\n"
    "  --blocks=200          Bloques por ronda (bridge)\n"
    "  --elems=256           Enteros por bloque (bridge)\n"
    "  --sleepms=0           Espera tras publicar/iterar (ms)\n"
    "\nEjemplos:\n"
    "  " << prog << " --sub\n"
    "  " << prog << " --pub --app=APP-1\n"
    "  " << prog << " --bridge --rounds=3 --blocks=300 --elems=512\n"
    "  " << prog << " --sub --bridge  (se suscribe y además genera eventos)\n";
}

// ====== ACTIVAR LEGACY AL FINAL DE LOS INCLUDES ======
#define MEMPROF_ENABLE_NEW_MACRO
#include "memprof.hpp"      // ¡nunca en headers y que no haya includes después!

// ====== main ======
int main(int argc, char** argv) {
  // Mantener WinSock vivo todo el proceso (no-op en Linux/Mac)
  net::WSAInit wsa_guard;

  setup_utf8_console();
  std::signal(SIGINT, on_sigint);

  // Defaults
  std::string host  = "127.0.0.1";
  uint16_t    port  = 5000;
  std::string topic = protocol::TOPIC_MEMORY_UPDATE;
  std::string appId = "APP-1";

  bool do_sub     = false;
  bool do_pub_sdk = false;
  bool do_pub_raw = false;
  bool do_bridge  = false;

  int rounds = 5, blocks = 200, elems = 256;
  int sleep_ms = 0;

  // Parse args muy simple
  for (int i = 1; i < argc; ++i) {
    std::string a = argv[i];
    auto eat = [&](const char* key, std::string& out) {
      if (a.rfind(key,0)==0) { out = a.substr(std::strlen(key)); return true; }
      return false;
    };
    auto eat_u16 = [&](const char* key, uint16_t& out) {
      if (a.rfind(key,0)==0) { out = static_cast<uint16_t>(std::stoi(a.substr(std::strlen(key)))); return true; }
      return false;
    };
    auto eat_int = [&](const char* key, int& out) {
      if (a.rfind(key,0)==0) { out = std::stoi(a.substr(std::strlen(key))); return true; }
      return false;
    };

    if      (a=="--sub")       do_sub = true;
    else if (a=="--pub")       do_pub_sdk = true;
    else if (a=="--pubraw")    do_pub_raw = true;
    else if (a=="--bridge")    do_bridge = true;
    else if (eat("--host=", host)) {}
    else if (eat_u16("--port=", port)) {}
    else if (eat("--topic=", topic)) {}
    else if (eat("--app=", appId)) {}
    else if (eat_int("--rounds=", rounds)) {}
    else if (eat_int("--blocks=", blocks)) {}
    else if (eat_int("--elems=",  elems)) {}
    else if (eat_int("--sleepms=", sleep_ms)) {}
    else { usage(argv[0]); return 1; }
  }

  // Config SDK para las operaciones por publish (y para el bridge)
  ProfilerClient::instance().configure(host, port, appId);

  // Suscripción (mantiene hilo lector)
  if (do_sub) {
    if (!subscribe_and_readloop(host, port, topic, "GUI-1"))
      return 2;
  }

  // Publicación simple (SDK o crudo)
  if (do_pub_sdk || do_pub_raw) {
    auto alloc = make_sample_json("ALLOC",  64, 1);
    auto freee = make_sample_json("FREE",    0, 2);

    if (do_pub_sdk) {
      publish_once_sdk(topic, alloc);
      publish_once_sdk(topic, freee);
    }
    if (do_pub_raw) {
      publish_once_raw(host, port, topic, alloc);
      publish_once_raw(host, port, topic, freee);
    }
  }

  // Bridge legacy -> broker + generación real de eventos
  if (do_bridge) {
    legacy_bridge::install_socket_sink(host, port, appId);
    generate_legacy_events(rounds, blocks, elems);
  }

  if (sleep_ms > 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(sleep_ms));
  }

  // Si solo hicimos --sub, mantenemos el proceso vivo hasta Ctrl+C
  if (do_sub && !do_pub_sdk && !do_pub_raw && !do_bridge) {
    std::cout << "[info] escuchando... (Ctrl+C para salir)\n";
    while (g_running) std::this_thread::sleep_for(std::chrono::milliseconds(100));
  }

  return 0;
}
