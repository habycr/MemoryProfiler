#include "Protocol.h"
#include <sstream>
#include <iomanip>
#include <cctype>

namespace protocol {

std::string encode(const std::string& in) {
    std::ostringstream out;
    out << std::uppercase << std::hex;
    for (unsigned char c : in) {
        if (c == '|' || c == '\n' || c == '%' || c == '\\') {
            out << '%' << std::setw(2) << std::setfill('0') << int(c);
        } else {
            out << c;
        }
    }
    return out.str();
}

static inline int hex2(int a, int b) {
    auto val = [](int x)->int {
        if (x >= '0' && x <= '9') return x - '0';
        if (x >= 'A' && x <= 'F') return x - 'A' + 10;
        if (x >= 'a' && x <= 'f') return x - 'a' + 10;
        return -1;
    };
    int hi = val(a), lo = val(b);
    if (hi < 0 || lo < 0) return -1;
    return (hi << 4) | lo;
}

std::string decode(const std::string& in) {
    std::string out; out.reserve(in.size());
    for (size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '%' && i + 2 < in.size()) {
            int v = hex2(in[i+1], in[i+2]);
            if (v >= 0) { out.push_back(static_cast<char>(v)); i += 2; }
            else out.push_back(in[i]);
        } else {
            out.push_back(in[i]);
        }
    }
    return out;
}

std::vector<std::string> split(const std::string& line) {
    std::vector<std::string> out;
    size_t start = 0, pos;
    while ((pos = line.find('|', start)) != std::string::npos) {
        out.push_back(decode(line.substr(start, pos - start)));
        start = pos + 1;
    }
    // último token (hasta fin o \n)
    if (start < line.size()) {
        std::string tail = line.substr(start);
        // eliminar '\n' de cola si viniera en el último token
        if (!tail.empty() && tail.back() == '\n') tail.pop_back();
        out.push_back(decode(tail));
    }
    return out;
}

std::optional<Request> parseLine(const std::string& line) {
    if (line.empty()) return std::nullopt;
    auto tokens = split(line);
    if (tokens.empty()) return std::nullopt;
    Request r;
    r.command = tokens[0];
    tokens.erase(tokens.begin());
    r.args = std::move(tokens);
    return r;
}

} // namespace protocol
