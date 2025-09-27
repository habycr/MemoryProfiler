#include <string>
#include <thread>
#include <atomic>
#include <vector>
#include <fstream>
#include <sstream>
#include <iostream>
#include <chrono>
#include <cctype>
#include <filesystem>
#include <iomanip>

#include "NetUtils.h"
#include "BrokerClient.h"
#include "Protocol.h"          // <-- para protocol::TOPIC_MEMORY_UPDATE
#include "MetricsAggregator.h" // agregador de métricas

#ifdef _WIN32
  #include <winsock2.h>
  #include <ws2tcpip.h>
#else
  #include <arpa/inet.h>
  #include <sys/socket.h>
  #include <unistd.h>
#endif

// ===== Utilidades HTTP =====
static std::string mime_for(const std::string& path) {
    auto dot = path.find_last_of('.');
    std::string ext = (dot == std::string::npos) ? "" : path.substr(dot + 1);
    for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
    if (ext == "html" || ext == "htm") return "text/html; charset=utf-8";
    if (ext == "js")   return "application/javascript; charset=utf-8";
    if (ext == "css")  return "text/css; charset=utf-8";
    if (ext == "json") return "application/json; charset=utf-8";
    if (ext == "png")  return "image/png";
    if (ext == "jpg" || ext == "jpeg") return "image/jpeg";
    if (ext == "svg")  return "image/svg+xml";
    if (ext == "ico")  return "image/x-icon";
    return "application/octet-stream";
}

static bool read_file(const std::filesystem::path& p, std::string& out) {
    std::ifstream ifs(p, std::ios::binary);
    if (!ifs) return false;
    std::ostringstream oss;
    oss << ifs.rdbuf();
    out = oss.str();
    return true;
}

static void http_send_response(socket_t s, int code, const char* status,
                               const char* content_type, const std::string& body) {
    std::ostringstream head;
    head << "HTTP/1.1 " << code << " " << status << "\r\n"
         << "Content-Type: " << content_type << "\r\n"
         << "Content-Length: " << body.size() << "\r\n"
         << "Cache-Control: no-store\r\n"
         << "Connection: close\r\n\r\n";
    const auto header = head.str();
    net::sendAll(s, header.data(), header.size());
    if (!body.empty()) net::sendAll(s, body.data(), body.size());
}

static bool http_read_request(socket_t s, std::string& method, std::string& path) {
    std::string req;
    char buf[2048];
    for (;;) {
        int n = ::recv(s, buf, sizeof(buf), 0);
        if (n <= 0) break;
        req.append(buf, buf + n);
        if (req.find("\r\n\r\n") != std::string::npos) break;
        if (req.size() > 16384) break;
    }
    auto pos = req.find("\r\n");
    if (pos == std::string::npos) return false;
    std::string line = req.substr(0, pos);

    std::istringstream iss(line);
    iss >> method >> path;
    if (method.empty() || path.empty()) return false;
    return true;
}

static std::string url_decode(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (size_t i=0; i<in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int v = 0;
            std::istringstream(in.substr(i+1,2)) >> std::hex >> v;
            out.push_back(static_cast<char>(v));
            i += 2;
        } else if (in[i] == '+') {
            out.push_back(' ');
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

static std::filesystem::path safe_join(const std::filesystem::path& root, std::string path) {
    if (path.empty() || path == "/") path = "/index.html";
    if (auto q = path.find('?'); q != std::string::npos) path = path.substr(0, q);
    path = url_decode(path);
    for (auto& c : path) if (c == '\\') c = '/';
    if (path.find("..") != std::string::npos) return {};
    if (!path.empty() && path.front() == '/') path.erase(0, 1);
    return root / path;
}

// ===== Bomba de eventos broker → agregador =====
struct PumpConfig {
    std::string host = "127.0.0.1";
    uint16_t    port = 5000;
    std::string app  = "GUI-HTTP";
    std::string topic= protocol::TOPIC_MEMORY_UPDATE; // <-- alineado con test_socket
};

static void broker_pump_thread(const PumpConfig cfg, MetricsAggregator* agg, std::atomic<bool>* stop_flag) {
    BrokerClient client;
    client.configure(cfg.host, cfg.port, cfg.app);

    while (!stop_flag->load()) {
        if (!client.isConnected()) {
            if (client.connect() && client.subscribe(cfg.topic)) {
                std::cerr << "[gui_http] Suscrito a " << cfg.topic << "\n";
            } else {
                std::this_thread::sleep_for(std::chrono::milliseconds(500));
                continue;
            }
        }
        std::string ev = client.receiveEvent(); // bloqueante
        if (ev.empty()) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            continue;
        }
        agg->processEvent(ev);
    }
}

// ===== Servidor HTTP principal =====
int main(int argc, char** argv) {
    net::WSAInit wsa;

    uint16_t http_port = 8080;
    std::filesystem::path static_dir = "../gui";
    PumpConfig pump;

    for (int i=1; i<argc; ++i) {
        std::string a = argv[i];
        auto getv = [&](const char* /*key*/)->std::string {
            auto p = a.find('=');
            return (p==std::string::npos) ? "" : a.substr(p+1);
        };
        if (a.rfind("--port=",0)==0) http_port = static_cast<uint16_t>(std::stoi(getv("--port=")));
        else if (a.rfind("--static-dir=",0)==0) static_dir = getv("--static-dir=");
        else if (a.rfind("--host=",0)==0) pump.host = getv("--host=");
        else if (a.rfind("--bport=",0)==0) pump.port = static_cast<uint16_t>(std::stoi(getv("--bport=")));
        else if (a.rfind("--app=",0)==0) pump.app = getv("--app=");
        else if (a.rfind("--topic=",0)==0) pump.topic = getv("--topic="); // opcional, por si quieres cambiarlo
    }

    try { static_dir = std::filesystem::weakly_canonical(static_dir); } catch (...) {}
    std::cerr << "[gui_http] Static dir: " << static_dir.string() << "\n";
    std::cerr << "[gui_http] Broker: " << pump.host << ":" << pump.port << " app=" << pump.app << " topic=" << pump.topic << "\n";
    std::cerr << "[gui_http] HTTP:   127.0.0.1:" << http_port << "\n";

    MetricsAggregator agg(4096);
    // agg.setLeakThresholdMs(30000);

    std::atomic<bool> stop{false};
    std::thread th_pump(broker_pump_thread, pump, &agg, &stop);

    socket_t srv = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (IS_INVALID(srv)) {
        std::cerr << "[gui_http] ERROR: no pude crear socket HTTP\n";
        stop.store(true);
        th_pump.join();
        return 1;
    }
    int yes = 1;
#ifdef _WIN32
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof(yes));
#else
    setsockopt(srv, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
#endif

    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(http_port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::bind(srv, (sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "[gui_http] ERROR: bind puerto " << http_port << "\n";
        CLOSESOCK(srv);
        stop.store(true);
        th_pump.join();
        return 1;
    }
    if (::listen(srv, 16) < 0) {
        std::cerr << "[gui_http] ERROR: listen\n";
        CLOSESOCK(srv);
        stop.store(true);
        th_pump.join();
        return 1;
    }

    std::cerr << "[gui_http] Sirviendo HTTP en http://127.0.0.1:" << http_port << "/ (ENTER para salir)\n";
    std::thread th_quit([&]{
        (void)getchar();
        stop.store(true);
#ifdef _WIN32
        ::closesocket(srv);
#else
        ::shutdown(srv, SHUT_RDWR);
        ::close(srv);
#endif
    });

    while (!stop.load()) {
        sockaddr_in cli{}; socklen_t clen = sizeof(cli);
        socket_t c = ::accept(srv, (sockaddr*)&cli, &clen);
        if (IS_INVALID(c)) {
            if (stop.load()) break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            continue;
        }

        std::string method, path;
        if (!http_read_request(c, method, path)) {
            http_send_response(c, 400, "Bad Request", "text/plain; charset=utf-8", "Bad Request");
            CLOSESOCK(c); continue;
        }
        if (method != "GET") {
            http_send_response(c, 405, "Method Not Allowed", "text/plain; charset=utf-8", "Only GET");
            CLOSESOCK(c); continue;
        }

        // ====== ENDPOINTS JSON ======
        if (path == "/metrics") {
            uint64_t cur=0, peak=0, active=0, total=0, leak=0;
            agg.getMetrics(cur, peak, active, total, leak);
            std::ostringstream oss;
            oss << "{"
                << "\"current_bytes\":" << cur << ","
                << "\"peak_bytes\":"    << peak << ","
                << "\"active_allocs\":" << active << ","
                << "\"total_allocs\":"  << total << ","
                << "\"leak_bytes\":"    << leak
                << "}";
            http_send_response(c, 200, "OK", "application/json; charset=utf-8", oss.str());
            CLOSESOCK(c); continue;
        }

        if (path == "/timeline") {
            auto t = agg.getTimeline();
            std::ostringstream oss;
            oss << "[";
            for (size_t i=0; i<t.size(); ++i) {
                if (i) oss << ",";
                oss << "{"
                    << "\"t_ns\":" << t[i].t_ns << ","
                    << "\"current_bytes\":" << t[i].current_bytes << ","
                    << "\"leak_bytes\":"    << t[i].leak_bytes
                    << "}";
            }
            oss << "]";
            http_send_response(c, 200, "OK", "application/json; charset=utf-8", oss.str());
            CLOSESOCK(c); continue;
        }

        if (path == "/blocks") {
            auto v = agg.getBlocks();
            auto q = [](const std::string& s){ std::ostringstream o; o << "\"";
                for (char ch: s){ if (ch=='"'||ch=='\\') o<<'\\'<<ch; else o<<ch; } o<<"\""; return o.str(); };

            std::ostringstream oss;
            oss << "[";
            for (size_t i=0; i<v.size(); ++i) {
                if (i) oss << ",";
                oss << "{"
                    << "\"ptr\":"      << q(v[i].ptr)  << ","
                    << "\"size\":"     << v[i].size    << ","
                    << "\"file\":"     << q(v[i].file) << ","  // <-- corregido
                    << "\"line\":"     << v[i].line    << ","
                    << "\"type\":"     << q(v[i].type) << ","
                    << "\"is_array\":" << (v[i].is_array ? "true" : "false") << ","
                    << "\"ts_ns\":"    << v[i].ts_ns
                    << "}";
            }
            oss << "]";
            http_send_response(c, 200, "OK", "application/json; charset=utf-8", oss.str());
            CLOSESOCK(c); continue;
        }

        if (path == "/file-stats") {
            auto m = agg.getFileStats();
            auto qk = [](const std::string& s){ std::ostringstream o; o << "\"";
                for (char ch: s){ if (ch=='"'||ch=='\\') o<<'\\'<<ch; else o<<ch; } o<<"\""; return o.str(); };

            std::ostringstream oss;
            oss << "{";
            bool first = true;
            for (const auto& kv : m) {
                if (!first) oss << ",";
                first = false;
                oss << qk(kv.first) << ":{"
                    << "\"alloc_count\":" << kv.second.alloc_count << ","
                    << "\"alloc_bytes\":" << kv.second.alloc_bytes << ","
                    << "\"live_count\":"  << kv.second.live_count  << ","
                    << "\"live_bytes\":"  << kv.second.live_bytes
                    << "}";
            }
            oss << "}";
            http_send_response(c, 200, "OK", "application/json; charset=utf-8", oss.str());
            CLOSESOCK(c); continue;
        }

        if (path == "/leaks") {
            auto k = agg.getLeaksKPIs();
            auto q = [](const std::string& s){ std::ostringstream o; o << "\"";
                for (char ch: s){ if (ch=='"'||ch=='\\') o<<'\\'<<ch; else o<<ch; } o<<"\""; return o.str(); };
            std::ostringstream oss;
            oss << "{"
                << "\"total_leak_bytes\":" << k.total_leak_bytes << ","
                << std::fixed << std::setprecision(6)
                << "\"leak_rate\":"        << k.leak_rate << ","
                << "\"largest\":{"
                    << "\"file\":" << q(k.largest.file) << ","
                    << "\"ptr\":"  << q(k.largest.ptr)  << ","
                    << "\"size\":" << k.largest.size
                << "},"
                << "\"top_file_by_leaks\":{"
                    << "\"file\":"  << q(k.top_file_by_leaks.file)  << ","
                    << "\"count\":" << k.top_file_by_leaks.count    << ","
                    << "\"bytes\":" << k.top_file_by_leaks.bytes
                << "}"
            << "}";
            http_send_response(c, 200, "OK", "application/json; charset=utf-8", oss.str());
            CLOSESOCK(c); continue;
        }

        // ===== estáticos =====
        std::filesystem::path full = safe_join(static_dir, path);
        std::string body;
        if (full.empty() || !read_file(full, body)) {
            http_send_response(c, 404, "Not Found", "text/plain; charset=utf-8", "404 Not Found");
            CLOSESOCK(c); continue;
        }
        const std::string ctype = mime_for(full.string());
        http_send_response(c, 200, "OK", ctype.c_str(), body);
        CLOSESOCK(c);
    }

    if (th_pump.joinable()) th_pump.join();
    if (th_quit.joinable()) th_quit.join();
    return 0;
}
