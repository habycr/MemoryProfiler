#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>
#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
#else
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#define closesocket close
#endif


static void sleep_ms(int ms){ std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }


int main(){
#ifdef _WIN32
WSADATA wsa; WSAStartup(MAKEWORD(2,2), &wsa);
#endif
int srv = socket(AF_INET, SOCK_STREAM, 0);
sockaddr_in addr{}; addr.sin_family = AF_INET; addr.sin_port = htons(7070); addr.sin_addr.s_addr = htonl(INADDR_ANY);
bind(srv, (sockaddr*)&addr, sizeof(addr)); listen(srv, 1);
printf("mock_broker escuchando en 7070...\n");
int cli = accept(srv, nullptr, nullptr); printf("cliente conectado\n");


// parámetros de bins/direcciones ficticias
unsigned long long base = 0x10000000ULL; unsigned long long hi = 0x20000000ULL; const int nBins = 64;
long long t = 0; long long heap = 0, peak = 0;


while (true) {
// generar 100 leaks vivos falsos repartidos
std::string per_file = "";
std::string leaks = "";
for (int i=0;i<100;++i){
unsigned long long ptr = base + (rand()%((int)(hi-base)));
int sz = (rand()%4096)+16; heap += sz; if (heap>peak) peak=heap; if (rand()%4==0) heap -= sz;
char buf[256];
snprintf(buf, sizeof(buf), "{\"ptr\":%llu,\"size\":%d,\"file\":\"file%02d.cpp\",\"line\":%d,\"type\":\"int*\",\"ts_ns\":%lld}",
ptr, sz, i%7, 10+(i%200), (long long)(t*1000000LL));
if (!leaks.empty()) leaks += ","; leaks += buf;
}
for (int f=0; f<7; ++f) {
char buf[256];
long long tot = 10000 + (rand()%20000); int allocs = 30+(rand()%150); int frees = allocs - (rand()%30);
snprintf(buf, sizeof(buf), "{\"file\":\"file%02d.cpp\",\"total_bytes\":%lld,\"allocs\":%d,\"frees\":%d,\"net_bytes\":%lld}", f, tot, allocs, frees, tot - frees*64);
if (!per_file.empty()) per_file += ","; per_file += buf;
}
std::string bins = "";
for (int b=0;b<nBins;++b){
unsigned long long lo = base + (unsigned long long)((double)b/nBins*(hi-base));
unsigned long long hh = base + (unsigned long long)((double)(b+1)/nBins*(hi-base));
int bytes = rand()%50000; int al = rand()%100;
char buf[256]; snprintf(buf, sizeof(buf), "{\"addr_lo\":%llu,\"addr_hi\":%llu,\"bytes\":%d,\"allocations\":%d}", lo, hh, bytes, al);
if (!bins.empty()) bins += ","; bins += buf;
}


char json[65536];
snprintf(json, sizeof(json),
"{\"kind\":\"SNAPSHOT\",\"general\":{\"heap_current\":%lld,\"heap_peak\":%lld,\"alloc_rate\":%.2f,\"free_rate\":%.2f,\"uptime_ms\":%lld},\"bins\":[%s],\"per_file\":[%s],\"leaks\":[%s]}\n",
heap, peak, 50.0 + (rand()%50), 40.0 + (rand()%40), t, bins.c_str(), per_file.c_str(), leaks.c_str());


send(cli, json, (int)strlen(json), 0);
t += 200; sleep_ms(200);
}
closesocket(cli); closesocket(srv);
#ifdef _WIN32
WSACleanup();
#endif
return 0;
}