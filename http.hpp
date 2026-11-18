#pragma once
// Tiny multi-threaded HTTP/1.1 server (no external libraries). Works on Linux, macOS and Windows.
#include <functional>
#include <map>
#include <string>

struct Request {
    std::string method, path, query, body;
    std::map<std::string, std::string> headers;      // keys are lower-case
    std::string param(const std::string& key) const; // value from the query string
};

struct Response {
    int status = 200;
    std::string type = "application/json";
    std::string body;
};

class HttpServer {
public:
    using Handler = std::function<Response(const Request&)>;
    void route(const std::string& method, const std::string& path, Handler h);
    void serveStatic(const std::string& dir) { staticDir = dir; }
    bool listen(int port);                            // blocks forever
private:
    std::map<std::string, Handler> routes;            // key = "METHOD /path"
    std::string staticDir;
    void handle(long long fd);
};
