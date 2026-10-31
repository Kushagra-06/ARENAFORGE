/*
 ArenaForge - C++ application layer
 RAII wrappers around the C DSA engine (arena_engine.h).

 The C engine owns every algorithm and data structure. These thin classes only
 add ownership (constructor/destructor) and exceptions, so the rest of the C++
 code never calls free() or checks a NULL by hand.
 */
#pragma once
#include "arena_engine.h"

#include <cstddef>
#include <new>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace arena {

/* ---------- 1. Sorting (ranking & seeding) ---------- */
enum class SortAlgo { Merge = 0, Quick = 1 };

inline void sortRanking(std::vector<RankEntry>& v, SortAlgo algo) {
    if (v.size() < 2) return;
    if (algo == SortAlgo::Merge) ae_merge_sort(v.data(), static_cast<int>(v.size()));
    else                         ae_quick_sort(v.data(), static_cast<int>(v.size()));
}

/* ---------- 6. Linked list (match history) ---------- */
class HistoryLog {
public:
    HistoryLog() : h_(history_create()) { if (!h_) throw std::bad_alloc(); }
    ~HistoryLog() { history_free(h_); }
    HistoryLog(const HistoryLog&) = delete;
    HistoryLog& operator=(const HistoryLog&) = delete;

    void append(const HistNode& rec) { if (!history_append(h_, &rec)) throw std::bad_alloc(); }
    int meetings(int a, int b) const { return history_meetings(h_, a, b); }
    int count() const { return history_count(h_); }
    const MatchHistory* raw() const { return h_; }

    /* walk the list head -> tail (chronological order) */
    std::vector<HistNode> records() const {
        std::vector<HistNode> out;
        for (const HistNode* n = history_head(h_); n; n = n->next) {
            HistNode c = *n;
            c.next = nullptr;
            out.push_back(c);
        }
        return out;
    }

private:
    MatchHistory* h_;
};

/* ---------- 4. Hash table (name -> id, case-insensitive) ---------- */
class NameIndex {
public:
    explicit NameIndex(int buckets = 64) : t_(ht_create(buckets)) { if (!t_) throw std::bad_alloc(); }
    ~NameIndex() { ht_free(t_); }
    NameIndex(const NameIndex&) = delete;
    NameIndex& operator=(const NameIndex&) = delete;

    void put(const std::string& key, int value) {
        if (!ht_put(t_, key.c_str(), value)) throw std::bad_alloc();
    }
    std::optional<int> find(const std::string& key) const {
        int v = 0;
        if (ht_get(t_, key.c_str(), &v)) return v;
        return std::nullopt;
    }
    bool remove(const std::string& key) { return ht_remove(t_, key.c_str()) == 1; }
    int count() const { return ht_count(t_); }
    void clear() {
        HashTable* fresh = ht_create(64);
        if (!fresh) throw std::bad_alloc();
        ht_free(t_);
        t_ = fresh;
    }

private:
    HashTable* t_;
};

/* ---------- 5. Queue (registration waitlist) ---------- */
class WaitQueue {
public:
    WaitQueue() : q_(queue_create()) { if (!q_) throw std::bad_alloc(); }
    ~WaitQueue() { queue_free(q_); }
    WaitQueue(const WaitQueue&) = delete;
    WaitQueue& operator=(const WaitQueue&) = delete;

    void enqueue(int v) { if (!queue_enqueue(q_, v)) throw std::bad_alloc(); }
    bool dequeue(int& out) { return queue_dequeue(q_, &out) == 1; }
    bool remove(int v) { return queue_remove(q_, v) == 1; }
    int size() const { return queue_size(q_); }
    bool contains(int v) const {
        for (const QNode* n = q_->head; n; n = n->next) if (n->value == v) return true;
        return false;
    }
    std::vector<int> toVector() const {
        std::vector<int> out;
        for (const QNode* n = q_->head; n; n = n->next) out.push_back(n->value);
        return out;
    }

private:
    IntQueue* q_;
};

/* ---------- 2. Heap / priority queue (matchmaking candidates) ---------- */
class PairHeap {
public:
    explicit PairHeap(int cap = 16) : h_(heap_create(cap)) { if (!h_) throw std::bad_alloc(); }
    ~PairHeap() { heap_free(h_); }
    PairHeap(const PairHeap&) = delete;
    PairHeap& operator=(const PairHeap&) = delete;

    void push(const MatchPair& p) { if (!heap_push(h_, p)) throw std::bad_alloc(); }
    bool pop(MatchPair& out) { return heap_pop(h_, &out) == 1; }
    int size() const { return heap_size(h_); }

private:
    MinHeap* h_;
};

/* ---------- 3. Tree (knockout bracket) ---------- */
class BracketTree {
public:
    BracketTree() = default;
    ~BracketTree() { clear(); }
    BracketTree(const BracketTree&) = delete;
    BracketTree& operator=(const BracketTree&) = delete;

    void clear() { bracket_free(root_); root_ = nullptr; }
    bool build(const std::vector<int>& seeded) {
        clear();
        root_ = bracket_build(seeded.data(), static_cast<int>(seeded.size()));
        return root_ != nullptr;
    }
    const BracketNode* root() const { return root_; }

    /* matches whose two participants are known but that have no winner yet */
    std::vector<BracketNode*> ready() {
        std::vector<BracketNode*> out;
        if (!root_) return out;
        int cap = 1 << root_->round;                 /* leaves >= internal nodes */
        out.resize(static_cast<std::size_t>(cap));
        int k = bracket_collect_ready(root_, out.data(), cap);
        out.resize(static_cast<std::size_t>(k));
        return out;
    }
    bool reportWinner(int nodeId, int winner) { return bracket_report_winner(root_, nodeId, winner) == 1; }
    int champion() const { return bracket_champion(root_); }

private:
    BracketNode* root_ = nullptr;
};

/* ---------- 7. Graph (scheduling conflicts + colouring) ---------- */
struct SchedulePlan {
    std::vector<int> colors;                       /* graph colour of each match            */
    int colorCount = 0;                            /* colours used (Welsh-Powell result)    */
    std::vector<int> slots;                        /* time slot of each match               */
    int slotCount = 0;
    std::vector<std::pair<int, int>> edges;        /* conflicting match pairs (i < j)       */
};

/*
 Two matches conflict when they share a team. The C engine builds the conflict
 graph and colours it; every colour is a set of matches that may run together.
 `stations` is how many matches can be played at the same time (arenas), so a
 colour class bigger than that is spread over several consecutive time slots.
 */
inline SchedulePlan planSchedule(const std::vector<std::pair<int, int>>& pairs, int stations) {
    SchedulePlan plan;
    const int m = static_cast<int>(pairs.size());
    if (m == 0) return plan;
    if (stations < 1) stations = 1;

    std::vector<int> ta(static_cast<std::size_t>(m)), tb(static_cast<std::size_t>(m));
    for (int i = 0; i < m; ++i) { ta[i] = pairs[i].first; tb[i] = pairs[i].second; }

    Graph* g = graph_build_conflicts(ta.data(), tb.data(), m);
    if (!g) throw std::bad_alloc();
    plan.colors.assign(static_cast<std::size_t>(m), -1);
    plan.colorCount = graph_color(g, plan.colors.data());
    for (int u = 0; u < m; ++u)
        for (int k = 0; k < graph_degree(g, u); ++k) {
            int v = g->adj[u][k];
            if (u < v) plan.edges.emplace_back(u, v);
        }
    graph_free(g);

    plan.slots.assign(static_cast<std::size_t>(m), 0);
    int slot = 0;
    for (int c = 0; c < plan.colorCount; ++c) {
        int inSlot = 0;
        bool any = false;
        for (int i = 0; i < m; ++i) {
            if (plan.colors[i] != c) continue;
            if (inSlot == stations) { ++slot; inSlot = 0; }
            plan.slots[i] = slot;
            ++inSlot;
            any = true;
        }
        if (any) ++slot;
    }
    plan.slotCount = slot;
    return plan;
}

} // namespace arena
