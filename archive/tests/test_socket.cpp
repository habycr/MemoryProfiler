// tests/test_socket.cpp
// Test interactivo con menú: SUBSCRIBE / PUBLISH / LEGACY-BRIDGE

#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <cstring>
#include <cstdint>
#include <csignal>
#include <cstdio>       // std::snprintf
#include <mutex>
#include <filesystem>

#include "Protocol.h"
#include "MemoryTracker.h"  // ProfilerClient (SDK)
#include "NetUtils.h"
#include "LegacyBridge.h"   // instalar sink legacy -> broker

// ===== Consola UTF-8 en Windows (opcional) =====
#ifdef _WIN32
  #ifndef WIN32_LEAN_AND_MEAN
  #define WIN32_LEAN_AND_MEAN
  #endif
  #ifndef NOMINMAX
  #define NOMINMAX
  #endif
  #include <windows.h>
  static void setup_utf8_console() {
    SetConsoleOutputCP(CP_UTF8);
    SetConsoleCP(CP_UTF8);
  }
#else
  static void setup_utf8_console() {}
#endif

#ifdef _WIN32
  constexpr socket_t INVALID_SOCK = INVALID_SOCKET;
#else
  constexpr socket_t INVALID_SOCK = (socket_t)-1;
#endif

// ===== Señal de salida segura =====
static std::atomic<bool> g_running{true};
static void on_sigint(int) { g_running = false; }

// ===== Helpers red =====
static socket_t connect_to(const std::string& ip, uint16_t port) {
  socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (IS_INVALID(s)) return INVALID_SOCK;
  sockaddr_in a{}; a.sin_family = AF_INET; a.sin_port = htons(port);
  if (::inet_pton(AF_INET, ip.c_str(), &a.sin_addr) != 1) { CLOSESOCK(s); return INVALID_SOCK; }
  if (::connect(s, (sockaddr*)&a, sizeof(a)) < 0)          { CLOSESOCK(s); return INVALID_SOCK; }
  return s;
}
static bool send_line(socket_t s, const std::string& line) {
  return net::sendAll(s, line.c_str(), line.size());
}

// ===== Auto-lanzar broker si no está (Windows) =====
#ifdef _WIN32
static bool ensure_broker_running(const std::string& host, uint16_t port, const char* argv0) {
  socket_t s = connect_to(host, port);
  if (!IS_INVALID(s)) { CLOSESOCK(s); return true; }
  std::filesystem::path exeDir = std::filesystem::path(argv0).parent_path();
  std::filesystem::path serverPath = exeDir / "memory_profiler_server.exe";
  if (!std::filesystem::exists(serverPath)) return false;
  std::string cmd = "\"" + serverPath.string() + "\" " + host + " " + std::to_string(port);
  STARTUPINFOA si{}; si.cb = sizeof(si); PROCESS_INFORMATION pi{};
  if (!CreateProcessA(NULL, cmd.data(), NULL, NULL, FALSE, CREATE_NEW_CONSOLE, NULL, exeDir.string().c_str(), &si, &pi))
    return false;
  CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
  std::this_thread::sleep_for(std::chrono::milliseconds(300));
  s = connect_to(host, port);
  if (!IS_INVALID(s)) { CLOSESOCK(s); return true; }
  return false;
}
#endif

// ===== Estado de la app =====
struct AppState {
  std::string host = "127.0.0.1";
  uint16_t    port = 5000;
  std::string topic = protocol::TOPIC_MEMORY_UPDATE;
  std::string appId = "APP-1";

  // Suscriptor
  std::atomic<bool> sub_running{false};
  socket_t sub_sock = INVALID_SOCK;
  std::thread sub_thread;

  // Bridge
  std::atomic<bool> bridge_installed{false};

  std::mutex m; // proteger sub_sock
};
static AppState G;

// ===== Suscripción (start/stop) =====
static bool start_subscriber() {
  std::lock_guard<std::mutex> lk(G.m);
  if (G.sub_running) { std::cout << "Ya estás suscrito.\n"; return true; }
  G.sub_sock = connect_to(G.host, G.port);
  if (IS_INVALID(G.sub_sock)) { std::cout << "[sub] no se pudo conectar\n"; return false; }

  std::string line = std::string("SUBSCRIBE|") + protocol::encode(G.topic) + "|" + protocol::encode("GUI-1") + "\n";
  if (!send_line(G.sub_sock, line)) { std::cout << "[sub] fallo send\n"; CLOSESOCK(G.sub_sock); G.sub_sock = INVALID_SOCK; return false; }

  std::string resp;
  if (!net::recvLine(G.sub_sock, resp)) { std::cout << "[sub] sin respuesta\n"; CLOSESOCK(G.sub_sock); G.sub_sock = INVALID_SOCK; return false; }
  std::cout << "[sub] resp: " << resp << "\n";
  if (resp.rfind("OK", 0) != 0) { CLOSESOCK(G.sub_sock); G.sub_sock = INVALID_SOCK; return false; }

  G.sub_running = true;
  G.sub_thread = std::thread([]{
    std::string r;
    while (G.sub_running && net::recvLine(G.sub_sock, r)) {
      if (r.rfind("PUBLISH|", 0) == 0) std::cout << "[RX] " << r << "\n";
      r.clear();
    }
    std::lock_guard<std::mutex> lk(G.m);
    if (!IS_INVALID(G.sub_sock)) { CLOSESOCK(G.sub_sock); G.sub_sock = INVALID_SOCK; }
    G.sub_running = false;
  });
  return true;
}
static void stop_subscriber() {
  std::thread th;
  {
    std::lock_guard<std::mutex> lk(G.m);
    if (!G.sub_running) return;
    G.sub_running = false;
    if (!IS_INVALID(G.sub_sock)) { CLOSESOCK(G.sub_sock); G.sub_sock = INVALID_SOCK; }
    th = std::move(G.sub_thread);
  }
  if (th.joinable()) th.join();
}

// ===== Publicar ejemplos =====
static std::string make_sample_json(const char* kind, std::size_t bytes, int iter) {
  char buf[512];
  auto now = std::chrono::high_resolution_clock::now().time_since_epoch();
  auto ns  = std::chrono::duration_cast<std::chrono::nanoseconds>(now).count();

  // is_array: 1 si "ALLOC", 0 si no
  int is_array = (std::string(kind) == "ALLOC") ? 1 : 0;

  std::snprintf(buf, sizeof(buf),
    "{\"kind\":\"%s\",\"ptr\":\"0x%08X\",\"size\":%zu,"
    "\"file\":\"\",\"line\":0,\"type\":\"\",\"is_array\":%d,"
    "\"ts_ns\":%lld,\"thread\":%lld}",
    kind, 0xDEAD0000 + iter, bytes, is_array,
    (long long)ns, (long long)std::hash<std::thread::id>{}(std::this_thread::get_id()));
  return std::string(buf);
}
static void publish_sdk() {
  auto j1 = make_sample_json("ALLOC", 256, 1);
  auto j2 = make_sample_json("FREE",    0, 2);
  ProfilerClient::instance().configure(G.host, G.port, G.appId);
  bool a = ProfilerClient::instance().publish(G.topic, j1);
  bool b = ProfilerClient::instance().publish(G.topic, j2);
  std::cout << "[pub SDK] " << (a && b ? "OK" : "FALLO") << "\n";
}
static void publish_raw() {
  socket_t s = connect_to(G.host, G.port);
  if (IS_INVALID(s)) { std::cout << "[pub raw] no conecta\n"; return; }
  for (auto& j : { make_sample_json("ALLOC", 64, 3), make_sample_json("FREE", 0, 4) }) {
    std::string line = std::string("PUBLISH|") + protocol::encode(G.topic) + "|" + protocol::encode(j) + "|" + protocol::encode(G.appId) + "\n";
    if (!send_line(s, line)) { std::cout << "[pub raw] fallo send\n"; break; }
  }
  CLOSESOCK(s);
  std::cout << "[pub raw] OK\n";
}

// ===== Generar eventos reales (legacy) =====
static void generate_legacy_events(int rounds, int blocks, int elems) {
  for (int r = 0; r < rounds && g_running; ++r) {
    std::vector<int*> v; v.reserve(blocks);
    for (int i = 0; i < blocks; ++i) v.push_back(new int[elems]);
    for (int* p : v) delete[] p;
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
  }
}
static void bridge_burst() {
  ProfilerClient::instance().configure(G.host, G.port, G.appId);
  if (!G.bridge_installed) {
    legacy_bridge::install_socket_sink(G.host, G.port, G.appId);
    G.bridge_installed = true;
  }
  int rounds=3, blocks=300, elems=256;
  std::cout << "rounds? [3] ";   std::string s; std::getline(std::cin, s); if (!s.empty()) rounds = std::stoi(s);
  std::cout << "blocks? [300] "; std::getline(std::cin, s); if (!s.empty()) blocks = std::stoi(s);
  std::cout << "elems?  [256] "; std::getline(std::cin, s); if (!s.empty()) elems  = std::stoi(s);
  std::cout << "[bridge] generando...\n";
  generate_legacy_events(rounds, blocks, elems);
  std::cout << "[bridge] listo.\n";
}

// ===== Menú =====
static void print_menu() {
  std::cout <<
    "\n=== MemoryProfiler test (menu) ===\n"
    "Host: " << G.host << "  Port: " << G.port << "  AppId: " << G.appId << "\n"
    "1) Suscribirse y escuchar\n"
    "2) Parar suscripción\n"
    "3) Publicar ejemplo (SDK)\n"
    "4) Publicar ejemplo (raw)\n"
    "5) Generar eventos REALes (legacy new/delete)\n"
    "6) Cambiar host/puerto/appId\n"
    "7) Salir\n"
    "> ";
}

// ===== ACTIVAR LEGACY AL FINAL DE LOS INCLUDES =====
#define MEMPROF_ENABLE_NEW_MACRO
#include "memprof.hpp"   // ¡nunca en headers y que no haya includes después!

int main(int /*argc*/, char** argv) {
  net::WSAInit wsa_guard;  // mantiene WinSock vivo (no-op en Linux)
  setup_utf8_console();
  std::signal(SIGINT, on_sigint);

#ifdef _WIN32
  (void)ensure_broker_running(G.host, G.port, argv[0]); // intenta arrancarlo si no responde
#endif

  std::string in;
  while (g_running) {
    print_menu();
    if (!std::getline(std::cin, in)) break;
    if (in.empty()) continue;

    switch (in[0]) {
      case '1': { if (start_subscriber()) std::cout << "[sub] escuchando...\n"; } break;
      case '2': { stop_subscriber(); std::cout << "[sub] parado.\n"; } break;
      case '3': publish_sdk(); break;
      case '4': publish_raw(); break;
      case '5': bridge_burst(); break;
      case '6': {
        std::string s;
        std::cout << "host [" << G.host << "]: ";   std::getline(std::cin, s); if (!s.empty()) G.host = s;
        std::cout << "port [" << G.port << "]: ";   std::getline(std::cin, s); if (!s.empty()) G.port = static_cast<uint16_t>(std::stoi(s));
        std::cout << "app  [" << G.appId << "]: ";  std::getline(std::cin, s); if (!s.empty()) G.appId = s;
        std::cout << "OK.\n";
      } break;
      case '7': g_running = false; break;
      default: std::cout << "Opción inválida.\n"; break;
    }
  }

  stop_subscriber();
  std::cout << "Bye!\n";
  return 0;
}

