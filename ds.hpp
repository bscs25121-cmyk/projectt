#pragma once
// Hand-written data structures for Zixel (DSA course project).
#include <cstddef>
#include <map>
#include <string>
#include <utility>
#include <vector>

// Stack: array-backed LIFO. When full, the oldest entry is dropped (bounded undo history).
template <typename T> class Stack {
    std::vector<T> a;
    size_t cap;
public:
    explicit Stack(size_t cap = 50) : cap(cap) {}
    void push(const T& v) { if (a.size() == cap) a.erase(a.begin()); a.push_back(v); }
    bool pop(T& out) { if (a.empty()) return false; out = a.back(); a.pop_back(); return true; }
    bool empty() const { return a.empty(); }
    size_t size() const { return a.size(); }
};

// Queue: singly linked FIFO, O(1) push and pop.
template <typename T> class Queue {
    struct Node { T v; Node* next; };
    Node *head = nullptr, *tail = nullptr;
    size_t n = 0;
public:
    Queue() = default;
    Queue(const Queue&) = delete;
    Queue& operator=(const Queue&) = delete;
    ~Queue() { T t; while (pop(t)) {} }
    void push(const T& v) { Node* x = new Node{v, nullptr}; if (tail) tail->next = x; else head = x; tail = x; ++n; }
    bool pop(T& out) {
        if (!head) return false;
        Node* x = head; out = x->v; head = x->next;
        if (!head) tail = nullptr;
        delete x; --n; return true;
    }
    bool empty() const { return n == 0; }
    size_t size() const { return n; }
};

// MinHeap: binary heap, smallest element (per Less) on top. O(log n) push/pop.
template <typename T, typename Less> class MinHeap {
    std::vector<T> a;
    Less less;
    void up(size_t i) { while (i > 0) { size_t p = (i - 1) / 2; if (!less(a[i], a[p])) break; std::swap(a[i], a[p]); i = p; } }
    void down(size_t i) {
        for (;;) {
            size_t l = 2 * i + 1, r = l + 1, m = i;
            if (l < a.size() && less(a[l], a[m])) m = l;
            if (r < a.size() && less(a[r], a[m])) m = r;
            if (m == i) break;
            std::swap(a[i], a[m]); i = m;
        }
    }
public:
    void push(const T& v) { a.push_back(v); up(a.size() - 1); }
    const T& top() const { return a[0]; }
    void pop() { a[0] = a.back(); a.pop_back(); if (!a.empty()) down(0); }
    size_t size() const { return a.size(); }
    bool empty() const { return a.empty(); }
};

// Trie: prefix search. Each key stores the ids of the entries that end there.
class Trie {
    struct Node { std::map<char, Node*> kid; std::vector<int> ids; };
    Node* root = new Node;
    static void destroy(Node* n) { for (auto& k : n->kid) destroy(k.second); delete n; }
    static void collect(const Node* n, std::vector<int>& out, size_t limit) {
        for (int id : n->ids) {
            if (out.size() >= limit) return;
            bool dup = false;
            for (int o : out) if (o == id) dup = true;
            if (!dup) out.push_back(id);
        }
        for (auto& k : n->kid) { if (out.size() >= limit) return; collect(k.second, out, limit); }
    }
public:
    Trie() = default;
    Trie(const Trie&) = delete;
    Trie& operator=(const Trie&) = delete;
    ~Trie() { destroy(root); }
    void insert(const std::string& key, int id) {
        Node* n = root;
        for (char c : key) { Node*& p = n->kid[c]; if (!p) p = new Node; n = p; }
        n->ids.push_back(id);
    }
    std::vector<int> withPrefix(const std::string& p, size_t limit = 6) const {
        const Node* n = root;
        for (char c : p) { auto it = n->kid.find(c); if (it == n->kid.end()) return {}; n = it->second; }
        std::vector<int> out; collect(n, out, limit); return out;
    }
};
