#include "http.hpp"
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment(lib, "ws2_32.lib")
typedef SOCKET sock_t;
static void closeSock(sock_t s) { closesocket(s); }
#else
#include <arpa/inet.h>
#include <csignal>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
typedef int sock_t;
#define INVALID_SOCKET (-1)
static void closeSock(sock_t s) { close(s); }
#endif
#ifndef MSG_NOSIGNAL
#define MSG_NOSIGNAL 0
#endif

static std::string urlDecode(const std::string& s) {
    std::string o;
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == '+') o += ' ';
        else if (s[i] == '%' && i + 2 < s.size()) { o += (char)strtol(s.substr(i + 1, 2).c_str(), nullptr, 16); i += 2; }
        else o += s[i];
    }
    return o;
}

std::string Request::param(const std::string& key) const {
    size_t i = 0;
    while (i < query.size()) {
        size_t amp = query.find('&', i);
        if (amp == std::string::npos) amp = query.size();
        std::string kv = query.substr(i, amp - i);
        size_t eq = kv.find('=');
        if (eq != std::string::npos && urlDecode(kv.substr(0, eq)) == key) return urlDecode(kv.substr(eq + 1));
        i = amp + 1;
    }
    return "";
}

static const char* statusText(int s) {
    switch (s) {
        case 200: return "OK"; case 400: return "Bad Request"; case 401: return "Unauthorized";
        case 404: return "Not Found"; case 409: return "Conflict"; case 429: return "Too Many Requests";
        default: return "Internal Server Error";
    }
}

static std::string mime(const std::string& p) {
    auto ends = [&](const char* e) { std::string s(e); return p.size() >= s.size() && p.compare(p.size() - s.size(), s.size(), s) == 0; };
    if (ends(".html")) return "text/html; charset=utf-8";
    if (ends(".css")) return "text/css; charset=utf-8";
    if (ends(".js")) return "application/javascript; charset=utf-8";
    if (ends(".svg")) return "image/svg+xml";
    if (ends(".png")) return "image/png";
    if (ends(".ico")) return "image/x-icon";
    return "application/octet-stream";
}

static bool sendAll(sock_t s, const std::string& d) {
    size_t o = 0;
    while (o < d.size()) {
        int n = (int)send(s, d.data() + o, (int)(d.size() - o), MSG_NOSIGNAL);
        if (n <= 0) return false;
        o += (size_t)n;
    }
    return true;
}

void HttpServer::route(const std::string& method, const std::string& path, Handler h) { routes[method + " " + path] = std::move(h); }

void HttpServer::handle(long long fd) {
    sock_t s = (sock_t)fd;
    std::string buf;
    char tmp[4096];
    size_t hend;
    while ((hend = buf.find("\r\n\r\n")) == std::string::npos) {
        int n = (int)recv(s, tmp, sizeof tmp, 0);
        if (n <= 0 || buf.size() > 65536) { closeSock(s); return; }
        buf.append(tmp, (size_t)n);
    }
    Request req;
    std::istringstream head(buf.substr(0, hend));
    std::string line, target;
    std::getline(head, line);
    std::istringstream rl(line);
    rl >> req.method >> target;
    while (std::getline(head, line)) {
        size_t c = line.find(':');
        if (c == std::string::npos) continue;
        std::string k = line.substr(0, c), v = line.substr(c + 1);
        for (char& ch : k) ch = (char)tolower((unsigned char)ch);
        while (!v.empty() && (v[0] == ' ' || v[0] == '\t')) v.erase(0, 1);
        while (!v.empty() && (v.back() == '\r' || v.back() == ' ')) v.pop_back();
        req.headers[k] = v;
    }
    size_t q = target.find('?');
    req.path = target.substr(0, q);
    if (q != std::string::npos) req.query = target.substr(q + 1);
    size_t len = req.headers.count("content-length") ? (size_t)strtoull(req.headers["content-length"].c_str(), nullptr, 10) : 0;
    if (len > 2 * 1024 * 1024) { closeSock(s); return; }
    req.body = buf.substr(hend + 4);
    while (req.body.size() < len) {
        int n = (int)recv(s, tmp, sizeof tmp, 0);
        if (n <= 0) { closeSock(s); return; }
        req.body.append(tmp, (size_t)n);
    }

    Response res;
    auto it = routes.find(req.method + " " + req.path);
    try {
        if (it != routes.end()) {
            res = it->second(req);
        } else if (req.method == "GET" && !staticDir.empty() && req.path.find("..") == std::string::npos) {
            std::string p = req.path == "/" ? "/index.html" : req.path;
            std::ifstream f(staticDir + p, std::ios::binary);
            if (f) { std::ostringstream ss; ss << f.rdbuf(); res.body = ss.str(); res.type = mime(p); }
            else { res.status = 404; res.type = "text/plain"; res.body = "Not found"; }
        } else {
            res.status = 404; res.type = "text/plain"; res.body = "Not found";
        }
    } catch (...) {
        res.status = 500; res.type = "application/json"; res.body = "{\"error\":\"server error\"}";
    }
    std::ostringstream out;
    out << "HTTP/1.1 " << res.status << " " << statusText(res.status) << "\r\nContent-Type: " << res.type
        << "\r\nContent-Length: " << res.body.size() << "\r\nCache-Control: no-store\r\nConnection: close\r\n\r\n" << res.body;
    sendAll(s, out.str());
    closeSock(s);
}

bool HttpServer::listen(int port) {
#ifdef _WIN32
    WSADATA w;
    WSAStartup(MAKEWORD(2, 2), &w);
#else
    signal(SIGPIPE, SIG_IGN);
#endif
    sock_t s = socket(AF_INET, SOCK_STREAM, 0);
    if (s == INVALID_SOCKET) return false;
    int yes = 1;
    setsockopt(s, SOL_SOCKET, SO_REUSEADDR, (const char*)&yes, sizeof yes);
    sockaddr_in a{};
    a.sin_family = AF_INET;
    a.sin_addr.s_addr = htonl(INADDR_ANY);
    a.sin_port = htons((unsigned short)port);
    if (bind(s, (sockaddr*)&a, sizeof a) < 0 || ::listen(s, 64) < 0) return false;
    for (;;) {
        sock_t c = accept(s, nullptr, nullptr);
        if (c == INVALID_SOCKET) continue;
        std::thread([this, c] { handle((long long)c); }).detach();
    }
}
