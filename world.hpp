#pragma once
// Zixel world state: chunked pixel canvas, users, paint queue, undo, search, persistence.
#include "ds.hpp"
#include <cstdint>
#include <deque>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

constexpr int W = 256, H = 128, CH = 32;          // canvas size in cells, chunk size
constexpr int MAXC = 31;                           // palette indexes 1..31 (0 = empty)
constexpr double MAX_CHARGES = 10, REGEN_SEC = 5;  // pixel "charges", one regenerates every 5 s
constexpr double STAMP_CD = 60;                    // one image stamp per minute
constexpr int STAMP_MAX = 576;                     // max pixels per stamp (24 x 24)

struct Chunk { uint8_t color[CH * CH] = {}; uint16_t owner[CH * CH] = {}; uint32_t when[CH * CH] = {}; };
struct Change { uint16_t x, y; uint8_t c; };
struct Px { int x, y, c; };
struct Undo { uint16_t x, y; uint8_t oldc, newc; uint16_t oldOwner; };
struct User {
    uint16_t id = 0;
    std::string name, token;
    double charges = MAX_CHARGES, lastTick = 0, stampReady = 0, lastSeen = 0;
    uint32_t painted = 0;
    Stack<Undo> undo{50};
};
struct Place { std::string name; int x, y; };
struct PaintReq { User* u; Px p; bool stamp; };
struct TopEntry { std::string name; uint32_t n; };

class World {
public:
    std::mutex mu;                 // callers lock this before using anything below
    bool dirty = false;

    explicit World(std::string savePath);
    ~World();
    static double now();           // seconds since the server started
    static Px cellOf(double lon, double lat);

    // sessions
    User* login(const std::string& name, std::string& err);
    User* byToken(const std::string& token);
    void refill(User& u);
    double nextIn(const User& u) const;
    int online() const;

    // canvas
    int submit(User& u, const std::vector<Px>& px, bool stamp, std::string& err);
    bool undo(User& u);
    uint8_t colorAt(int x, int y) const;
    int ownerAt(int x, int y) const;
    uint32_t whenAt(int x, int y) const;
    int regionSize(int x, int y) const;
    std::string cells() const;
    uint64_t version() const { return base + log.size(); }
    bool changesSince(uint64_t v, std::vector<Change>& out) const;
    std::vector<TopEntry> top(size_t k) const;
    const std::string& nameOf(int id) const;

    // search (places + artwork titles)
    void addPlace(const std::string& name, int x, int y);
    std::vector<Place> search(const std::string& q) const;

    // persistence
    void load();
    void save();

private:
    std::string savePath;
    std::unordered_map<int, Chunk> chunks;
    std::unordered_map<std::string, User*> byTok, byName;
    std::vector<User*> users;      // index = id - 1
    Queue<PaintReq> pending;
    std::deque<Change> log;
    uint64_t base = 0;
    std::vector<Place> places;
    Trie trie;
    Chunk& chunkFor(int x, int y);
    void apply(const PaintReq& r);
};
