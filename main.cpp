// Zixel server: routes, tiny JSON helpers, autosave. Build: see README.md
#include "http.hpp"
#include "world.hpp"
#include <algorithm>
#include <cctype>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <sstream>
#include <thread>

static World* gWorld = nullptr;

// ---------- tiny JSON helpers (only the shapes this app uses) ----------
static std::string jstr(const std::string& b, const std::string& key) {
    size_t k = b.find("\"" + key + "\"");
    if (k == std::string::npos) return "";
    k = b.find(':', k);
    if (k == std::string::npos) return "";
    k = b.find('"', k);
    if (k == std::string::npos) return "";
    size_t e = b.find('"', k + 1);
    return e == std::string::npos ? "" : b.substr(k + 1, e - k - 1);
}

static bool jtrue(const std::string& b, const std::string& key) {
    size_t k = b.find("\"" + key + "\"");
    if (k == std::string::npos) return false;
    k = b.find(':', k);
    if (k == std::string::npos) return false;
    k = b.find_first_not_of(' ', k + 1);
    return k != std::string::npos && b.compare(k, 4, "true") == 0;
}

static std::vector<Px> jpixels(const std::string& b) {   // "pixels":[[x,y,c],[x,y,c],...]
    std::vector<Px> out;
    size_t k = b.find("\"pixels\"");
    if (k == std::string::npos) return out;
    k = b.find('[', k);
    if (k == std::string::npos) return out;
    std::vector<int> nums;
    int depth = 0;
    for (size_t i = k; i < b.size(); i++) {
        char c = b[i];
        if (c == '[') depth++;
        else if (c == ']') { if (--depth == 0) break; }
        else if (isdigit((unsigned char)c)) {
            int v = 0;
            while (i < b.size() && isdigit((unsigned char)b[i])) { if (v < 100000) v = v * 10 + (b[i] - '0'); i++; }
            --i;
            nums.push_back(v);
            if (nums.size() > 3 * 700) break;
        }
    }
    for (size_t i = 0; i + 2 < nums.size(); i += 3) out.push_back({nums[i], nums[i + 1], nums[i + 2]});
    return out;
}

static std::string clean(const std::string& s, size_t maxLen) {   // letters, digits, space, _ and -
    std::string o;
    for (char c : s) if (isalnum((unsigned char)c) || c == '_' || c == '-' || c == ' ') o += c;
    while (!o.empty() && o.front() == ' ') o.erase(o.begin());
    while (!o.empty() && o.back() == ' ') o.pop_back();
    if (o.size() > maxLen) o.resize(maxLen);
    return o;
}

static Response J(const std::string& body, int status = 200) { Response r; r.status = status; r.body = body; return r; }
static Response Err(int status, const std::string& m) { return J("{\"error\":\"" + m + "\"}", status); }
static std::string num(double v) { char b[32]; snprintf(b, sizeof b, "%.2f", v); return b; }

static void onSignal(int) { if (gWorld) gWorld->save(); std::_Exit(0); }

int main() {
    const char* pe = getenv("PORT");
    int port = pe ? atoi(pe) : 8080;
    World world("zixel.save");
    gWorld = &world;
    signal(SIGINT, onSignal);
    signal(SIGTERM, onSignal);

    struct P { const char* n; double lon, lat; };
    const P places[] = {
        {"Lahore", 74.35, 31.55}, {"Karachi", 67.0, 24.86}, {"Islamabad", 73.05, 33.68}, {"Dubai", 55.3, 25.2},
        {"London", -0.12, 51.5}, {"Paris", 2.35, 48.85}, {"Berlin", 13.4, 52.5}, {"Moscow", 37.6, 55.75},
        {"Istanbul", 28.98, 41.0}, {"Cairo", 31.2, 30.05}, {"Nairobi", 36.8, -1.29}, {"Lagos", 3.4, 6.5},
        {"Cape Town", 18.4, -33.9}, {"Mumbai", 72.88, 19.07}, {"Delhi", 77.2, 28.6}, {"Beijing", 116.4, 39.9},
        {"Tokyo", 139.7, 35.7}, {"Seoul", 127.0, 37.55}, {"Singapore", 103.8, 1.35}, {"Jakarta", 106.8, -6.2},
        {"Sydney", 151.2, -33.87}, {"New York", -74.0, 40.7}, {"Toronto", -79.4, 43.65}, {"Los Angeles", -118.2, 34.05},
        {"San Francisco", -122.4, 37.77}, {"Mexico City", -99.1, 19.4}, {"Rio de Janeiro", -43.2, -22.9},
        {"Buenos Aires", -58.4, -34.6}};
    for (const P& p : places) { Px c = World::cellOf(p.lon, p.lat); world.addPlace(p.n, c.x, c.y); }

    HttpServer srv;
    srv.serveStatic("public");

    srv.route("POST", "/api/login", [&](const Request& r) -> Response {
        std::string name = clean(jstr(r.body, "name"), 16);
        if (name.size() < 2) return Err(400, "Please enter a name with at least 2 letters or numbers.");
        std::lock_guard<std::mutex> g(world.mu);
        std::string err;
        User* u = world.login(name, err);
        if (!u) return Err(409, err);
        return J("{\"token\":\"" + u->token + "\",\"name\":\"" + u->name + "\"}");
    });

    srv.route("GET", "/api/state", [&](const Request&) -> Response {
        std::lock_guard<std::mutex> g(world.mu);
        return J("{\"w\":" + std::to_string(W) + ",\"h\":" + std::to_string(H) + ",\"version\":" +
                 std::to_string(world.version()) + ",\"cells\":\"" + world.cells() + "\"}");
    });

    srv.route("GET", "/api/poll", [&](const Request& r) -> Response {
        std::lock_guard<std::mutex> g(world.mu);
        User* u = world.byToken(r.param("token"));
        if (!u) return Err(401, "session expired");
        world.refill(*u);
        uint64_t since = strtoull(r.param("since").c_str(), nullptr, 10);
        std::vector<Change> ch;
        bool ok = world.changesSince(since, ch);
        std::ostringstream o;
        o << "{\"version\":" << world.version() << ",\"reset\":" << (ok ? "false" : "true") << ",\"changes\":[";
        for (size_t i = 0; ok && i < ch.size(); i++) {
            if (i) o << ",";
            o << "[" << ch[i].x << "," << ch[i].y << "," << (int)ch[i].c << "]";
        }
        double stampIn = u->stampReady - World::now();
        o << "],\"charges\":" << num(u->charges) << ",\"max\":" << (int)MAX_CHARGES << ",\"nextIn\":" << num(world.nextIn(*u))
          << ",\"stampIn\":" << num(stampIn > 0 ? stampIn : 0) << ",\"painted\":" << u->painted
          << ",\"online\":" << world.online() << ",\"top\":[";
        auto top = world.top(5);
        for (size_t i = 0; i < top.size(); i++) {
            if (i) o << ",";
            o << "[\"" << top[i].name << "\"," << top[i].n << "]";
        }
        o << "]}";
        return J(o.str());
    });

    srv.route("POST", "/api/paint", [&](const Request& r) -> Response {
        std::lock_guard<std::mutex> g(world.mu);
        User* u = world.byToken(jstr(r.body, "token"));
        if (!u) return Err(401, "session expired");
        std::vector<Px> px = jpixels(r.body);
        bool stamp = jtrue(r.body, "stamp");
        if (px.empty()) return Err(400, "No pixels sent");
        if (!stamp && px.size() > 10) px.resize(10);
        std::string err;
        int n = world.submit(*u, px, stamp, err);
        if (n == 0) return Err(429, err);
        if (stamp) {
            std::string title = clean(jstr(r.body, "title"), 30);
            if (!title.empty()) {                                  // artwork becomes searchable
                int x0 = W, y0 = H, x1 = 0, y1 = 0;
                for (const Px& p : px) { x0 = std::min(x0, p.x); y0 = std::min(y0, p.y); x1 = std::max(x1, p.x); y1 = std::max(y1, p.y); }
                world.addPlace(title, (x0 + x1) / 2, (y0 + y1) / 2);
            }
        }
        return J("{\"placed\":" + std::to_string(n) + ",\"charges\":" + num(u->charges) + "}");
    });

    srv.route("POST", "/api/undo", [&](const Request& r) -> Response {
        std::lock_guard<std::mutex> g(world.mu);
        User* u = world.byToken(jstr(r.body, "token"));
        if (!u) return Err(401, "session expired");
        if (!world.undo(*u)) return Err(400, "Nothing to undo");
        return J("{\"ok\":true}");
    });

    srv.route("GET", "/api/pixel", [&](const Request& r) -> Response {
        int x = atoi(r.param("x").c_str()), y = atoi(r.param("y").c_str());
        if (x < 0 || y < 0 || x >= W || y >= H) return Err(400, "out of range");
        std::lock_guard<std::mutex> g(world.mu);
        int c = world.colorAt(x, y);
        std::ostringstream o;
        o << "{\"x\":" << x << ",\"y\":" << y << ",\"color\":" << c;
        if (c) {
            double ago = World::now() - world.whenAt(x, y);
            o << ",\"owner\":\"" << world.nameOf(world.ownerAt(x, y)) << "\",\"ago\":" << (ago > 0 ? (int)ago : 0)
              << ",\"region\":" << world.regionSize(x, y);
        }
        o << "}";
        return J(o.str());
    });

    srv.route("GET", "/api/search", [&](const Request& r) -> Response {
        std::lock_guard<std::mutex> g(world.mu);
        std::ostringstream o;
        o << "[";
        auto res = world.search(r.param("q"));
        for (size_t i = 0; i < res.size(); i++) {
            if (i) o << ",";
            o << "{\"name\":\"" << res[i].name << "\",\"x\":" << res[i].x << ",\"y\":" << res[i].y << "}";
        }
        o << "]";
        return J(o.str());
    });

    std::thread([&] {                                              // autosave every 20 s
        for (;;) {
            std::this_thread::sleep_for(std::chrono::seconds(20));
            std::lock_guard<std::mutex> g(world.mu);
            if (world.dirty) world.save();
        }
    }).detach();

    printf("Zixel is running: open http://localhost:%d\n", port);
    if (!srv.listen(port)) { fprintf(stderr, "Could not listen on port %d\n", port); return 1; }
    return 0;
}
