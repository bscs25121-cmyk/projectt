#include "world.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <random>

static const char* DIGITS = "0123456789abcdefghijklmnopqrstuv";

static std::string lower(std::string s) {
    for (char& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

static std::string randomToken() {
    static std::mt19937_64 rng{std::random_device{}()};
    char buf[40];
    snprintf(buf, sizeof buf, "%016llx%016llx", (unsigned long long)rng(), (unsigned long long)rng());
    return buf;
}

struct TopLess { bool operator()(const TopEntry& a, const TopEntry& b) const { return a.n < b.n; } };

double World::now() {
    static const auto t0 = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
}

Px World::cellOf(double lon, double lat) {
    int x = (int)(((lon + 180.0) * 3.5556) / 5.0), y = (int)(((90.0 - lat) * 3.5556) / 5.0);
    return {std::max(0, std::min(W - 1, x)), std::max(0, std::min(H - 1, y)), 0};
}

World::World(std::string path) : savePath(std::move(path)) { now(); load(); }
World::~World() { for (User* u : users) delete u; }

// ---------- sessions ----------
User* World::login(const std::string& name, std::string& err) {
    std::string key = lower(name);
    User* u;
    auto it = byName.find(key);
    if (it != byName.end()) {
        u = it->second;
        if (now() - u->lastSeen < 30) { err = "That name is being used right now. Try another one."; return nullptr; }
        byTok.erase(u->token);                       // name is free again: welcome back
    } else {
        u = new User;
        u->id = (uint16_t)(users.size() + 1);
        u->name = name;
        u->lastTick = now();
        users.push_back(u);
        byName[key] = u;
    }
    u->token = randomToken();
    u->lastSeen = now();
    byTok[u->token] = u;
    return u;
}

User* World::byToken(const std::string& token) {
    auto it = byTok.find(token);
    if (it == byTok.end()) return nullptr;
    it->second->lastSeen = now();
    return it->second;
}

void World::refill(User& u) {
    double t = now();
    u.charges = std::min(MAX_CHARGES, u.charges + (t - u.lastTick) / REGEN_SEC);
    u.lastTick = t;
}

double World::nextIn(const User& u) const {
    if (u.charges >= MAX_CHARGES) return 0;
    return (1.0 - (u.charges - std::floor(u.charges))) * REGEN_SEC;
}

int World::online() const {
    int n = 0;
    for (const User* u : users) if (now() - u->lastSeen < 15) ++n;
    return n;
}

// ---------- canvas ----------
Chunk& World::chunkFor(int x, int y) { return chunks[(y / CH) * (W / CH) + (x / CH)]; }

uint8_t World::colorAt(int x, int y) const {
    auto it = chunks.find((y / CH) * (W / CH) + (x / CH));
    return it == chunks.end() ? 0 : it->second.color[(y % CH) * CH + (x % CH)];
}
int World::ownerAt(int x, int y) const {
    auto it = chunks.find((y / CH) * (W / CH) + (x / CH));
    return it == chunks.end() ? 0 : it->second.owner[(y % CH) * CH + (x % CH)];
}
uint32_t World::whenAt(int x, int y) const {
    auto it = chunks.find((y / CH) * (W / CH) + (x / CH));
    return it == chunks.end() ? 0 : it->second.when[(y % CH) * CH + (x % CH)];
}

void World::apply(const PaintReq& r) {
    Chunk& ch = chunkFor(r.p.x, r.p.y);
    int i = (r.p.y % CH) * CH + (r.p.x % CH);
    if (ch.color[i] == r.p.c) return;                // nothing changes, nothing is charged
    if (!r.stamp) r.u->undo.push({(uint16_t)r.p.x, (uint16_t)r.p.y, ch.color[i], (uint8_t)r.p.c, ch.owner[i]});
    ch.color[i] = (uint8_t)r.p.c;
    ch.owner[i] = r.u->id;
    ch.when[i] = (uint32_t)now();
    log.push_back({(uint16_t)r.p.x, (uint16_t)r.p.y, (uint8_t)r.p.c});
    if (log.size() > 200000) { log.erase(log.begin(), log.begin() + 100000); base += 100000; }
    r.u->painted++;
    if (!r.stamp) r.u->charges -= 1;
    dirty = true;
}

int World::submit(User& u, const std::vector<Px>& px, bool stamp, std::string& err) {
    refill(u);
    size_t allowed;
    if (stamp) {
        if (now() < u.stampReady) { err = "Stamp cooldown: wait " + std::to_string((int)(u.stampReady - now()) + 1) + "s"; return 0; }
        allowed = STAMP_MAX;
    } else {
        allowed = (size_t)std::floor(u.charges);
        if (allowed == 0) { err = "No pixels left. Wait for them to recharge."; return 0; }
    }
    uint32_t before = u.painted;
    size_t taken = 0;
    for (const Px& p : px) {                          // 1) enqueue (FIFO)
        if (taken >= allowed) break;
        if (p.x < 0 || p.x >= W || p.y < 0 || p.y >= H || p.c < 1 || p.c > MAXC) continue;
        pending.push({&u, p, stamp});
        ++taken;
    }
    PaintReq r;
    while (pending.pop(r)) apply(r);                  // 2) drain in order
    int placed = (int)(u.painted - before);
    if (placed == 0) { err = "Nothing changed there."; return 0; }
    if (stamp) u.stampReady = now() + STAMP_CD;
    return placed;
}

bool World::undo(User& u) {                           // Stack: last pixel painted is first undone
    refill(u);
    Undo r;
    while (u.undo.pop(r)) {
        Chunk& ch = chunkFor(r.x, r.y);
        int i = (r.y % CH) * CH + (r.x % CH);
        if (ch.color[i] != r.newc || ch.owner[i] != u.id) continue;   // someone repainted it, skip
        ch.color[i] = r.oldc;
        ch.owner[i] = r.oldOwner;
        ch.when[i] = (uint32_t)now();
        log.push_back({r.x, r.y, r.oldc});
        u.charges = std::min(MAX_CHARGES, u.charges + 1);
        if (u.painted) u.painted--;
        dirty = true;
        return true;
    }
    return false;
}

// BFS flood fill: size of the connected same-colour region (capped).
int World::regionSize(int x, int y) const {
    uint8_t c = colorAt(x, y);
    if (!c) return 0;
    std::vector<char> seen(W * H, 0);
    Queue<int> q;
    q.push(y * W + x);
    seen[y * W + x] = 1;
    int n = 0, v;
    static const int dx[4] = {1, -1, 0, 0}, dy[4] = {0, 0, 1, -1};
    while (q.pop(v)) {
        if (++n >= 5000) break;
        int cx = v % W, cy = v / W;
        for (int k = 0; k < 4; k++) {
            int nx = cx + dx[k], ny = cy + dy[k];
            if (nx < 0 || ny < 0 || nx >= W || ny >= H || seen[ny * W + nx] || colorAt(nx, ny) != c) continue;
            seen[ny * W + nx] = 1;
            q.push(ny * W + nx);
        }
    }
    return n;
}

std::string World::cells() const {
    std::string s(W * H, '0');
    for (const auto& kv : chunks) {
        int cx = (kv.first % (W / CH)) * CH, cy = (kv.first / (W / CH)) * CH;
        for (int j = 0; j < CH; j++)
            for (int i = 0; i < CH; i++) s[(cy + j) * W + cx + i] = DIGITS[kv.second.color[j * CH + i]];
    }
    return s;
}

bool World::changesSince(uint64_t v, std::vector<Change>& out) const {
    if (v < base || v > version()) return false;      // too old or from a previous server run: reload
    size_t from = (size_t)(v - base);
    if (log.size() - from > 5000) return false;
    out.assign(log.begin() + from, log.end());
    return true;
}

// Leaderboard: min-heap of size k keeps the k biggest painters, O(n log k).
std::vector<TopEntry> World::top(size_t k) const {
    MinHeap<TopEntry, TopLess> h;
    for (const User* u : users) {
        if (!u->painted) continue;
        h.push({u->name, u->painted});
        if (h.size() > k) h.pop();
    }
    std::vector<TopEntry> out;
    while (!h.empty()) { out.push_back(h.top()); h.pop(); }
    std::reverse(out.begin(), out.end());
    return out;
}

const std::string& World::nameOf(int id) const {
    static const std::string none;
    return (id >= 1 && id <= (int)users.size()) ? users[id - 1]->name : none;
}

// ---------- search ----------
void World::addPlace(const std::string& name, int x, int y) {
    int id = (int)places.size();
    places.push_back({name, x, y});
    std::string k = lower(name);
    trie.insert(k, id);
    for (size_t i = 0; i + 1 < k.size(); i++)         // also index every word: "york" finds "New York"
        if (k[i] == ' ') trie.insert(k.substr(i + 1), id);
}

std::vector<Place> World::search(const std::string& q) const {
    std::vector<Place> r;
    std::string k = lower(q);
    if (k.empty()) return r;
    for (int id : trie.withPrefix(k, 6)) r.push_back(places[id]);
    return r;
}

// ---------- persistence ----------
void World::save() {
    FILE* f = fopen(savePath.c_str(), "wb");
    if (!f) return;
    std::string s = cells();
    fwrite(s.data(), 1, s.size(), f);
    fclose(f);
    dirty = false;
}

void World::load() {
    FILE* f = fopen(savePath.c_str(), "rb");
    if (!f) return;
    std::string s(W * H, '0');
    size_t n = fread(&s[0], 1, s.size(), f);
    fclose(f);
    if (n != s.size()) return;
    for (int i = 0; i < W * H; i++) {
        char c = s[i];
        int v = (c >= '0' && c <= '9') ? c - '0' : (c >= 'a' && c <= 'v') ? c - 'a' + 10 : 0;
        if (v) chunkFor(i % W, i / W).color[(i / W % CH) * CH + (i % W % CH)] = (uint8_t)v;
    }
}
