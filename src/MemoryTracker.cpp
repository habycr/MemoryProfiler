#include "MemoryTracker.h"
#include "Protocol.h"
#include "NetUtils.h"
#include <iostream>
#include <chrono>
#include <sstream>

#ifdef _WIN32
  #pragma comment(lib,"ws2_32.lib")
#endif

static inline uint64_t now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

ProfilerClient& ProfilerClient::instance() {
    static ProfilerClient inst;
    return inst;
}

ProfilerClient::ProfilerClient() {}
ProfilerClient::~ProfilerClient() {}

void ProfilerClient::configure(const std::string& ip, uint16_t port, const std::string& appId) {
    std::lock_guard<std::mutex> lock(mtx_);
    ip_ = ip; port_ = port; appId_ = appId;
}

socket_t ProfilerClient::connectOnce() {
    socket_t s = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
#ifdef _WIN32
    if (s == INVALID_SOCKET) return INVALID_SOCKET;
#else
    if (s < 0) return -1;
#endif
    sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(port_);
    if (::inet_pton(AF_INET, ip_.c_str(), &addr.sin_addr) != 1) {
        CLOSESOCK(s);
#ifdef _WIN32
        return INVALID_SOCKET;
#else
        return -1;
#endif
    }
    if (::connect(s, (sockaddr*)&addr, sizeof(addr)) < 0) {
        CLOSESOCK(s);
#ifdef _WIN32
        return INVALID_SOCKET;
#else
        return -1;
#endif
    }
    return s;
}

void ProfilerClient::publish(const std::string& topic, const std::string& payload) {
    std::lock_guard<std::mutex> lock(mtx_);
    socket_t s = connectOnce();
#ifdef _WIN32
    if (s == INVALID_SOCKET) return;
#else
    if (s < 0) return;
#endif
    std::string line = "PUBLISH|" + protocol::encode(topic) + "|" +
                       protocol::encode(payload) + "|" + protocol::encode(appId_) + "\n";
    net::sendAll(s, line.c_str(), line.size());
    std::string resp;
    net::recvLine(s, resp);
    (void)resp;
    CLOSESOCK(s);
}

MemoryStats& globalMemoryStats() {
    static MemoryStats stats;
    return stats;
}

#ifdef ENABLE_MEMORY_PROFILER
void* operator new(std::size_t sz) noexcept(false) {
    void* p = std::malloc(sz);
    if (!p) throw std::bad_alloc();
    auto& st = globalMemoryStats();
    auto cur = st.currentBytes.fetch_add(sz) + sz;
    auto maxv = st.maxBytes.load();
    while (cur > maxv && !st.maxBytes.compare_exchange_weak(maxv, cur)) {}
    st.activeAllocs.fetch_add(1);
    st.totalAllocs.fetch_add(1);

    std::ostringstream oss;
    oss << protocol::TOPIC_ALLOCATION << "|" << now_ms()
        << "|" << p << "|" << sz << "|new|unknown|0|" << "APP";
    ProfilerClient::instance().publish(protocol::TOPIC_ALLOCATION, oss.str());

    auto& s = globalMemoryStats();
    std::ostringstream mu;
    mu << protocol::TOPIC_MEMORY_UPDATE << "|" << now_ms()
       << "|" << (s.currentBytes.load() / 1048576.0)
       << "|" << s.activeAllocs.load()
       << "|" << (s.maxBytes.load() / 1048576.0)
       << "|" << s.totalAllocs.load()
       << "|" << "APP";
    ProfilerClient::instance().publish(protocol::TOPIC_MEMORY_UPDATE, mu.str());

    return p;
}

void operator delete(void* p) noexcept {
    if (!p) return;
    auto& st = globalMemoryStats();
    st.activeAllocs.fetch_sub(1);

    std::ostringstream oss;
    oss << protocol::TOPIC_DEALLOCATION << "|" << now_ms()
        << "|" << p << "|" << "APP";
    ProfilerClient::instance().publish(protocol::TOPIC_DEALLOCATION, oss.str());

    std::free(p);
}
#endif
