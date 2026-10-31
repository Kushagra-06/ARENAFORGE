/*
 ArenaForge - test cases
 Part A tests the C DSA engine directly. Part B tests the C++ application layer.
 Build and run:  make test
 */
#include "arena_system.hpp"
#include "json.hpp"
#include "snapshot.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <random>
#include <set>

using namespace arena;

static int g_checks = 0, g_failed = 0;
#define CHECK(cond)                                                                  \
    do {                                                                             \
        ++g_checks;                                                                  \
        if (!(cond)) { ++g_failed; std::printf("  FAIL %s:%d  %s\n", __FILE__, __LINE__, #cond); } \
    } while (0)

template <class E, class F>
static bool throwsType(F&& f) {
    try { f(); } catch (const E&) { return true; } catch (...) { return false; }
    return false;
}

static std::mt19937 rng(20260610);

/* ============================ Part A: C engine ============================ */

static bool ordered(const std::vector<RankEntry>& v) {
    for (std::size_t i = 1; i < v.size(); ++i) {
        if (v[i - 1].score < v[i].score) return false;
        if (v[i - 1].score == v[i].score && v[i - 1].id > v[i].id) return false;
    }
    return true;
}

static void testSorting() {
    std::printf("sorting (merge / quick)\n");
    for (int n : {0, 1, 2, 3, 10, 57, 200, 1000}) {
        std::vector<RankEntry> a;
        for (int i = 0; i < n; ++i) a.push_back({i, static_cast<double>(rng() % 20)});   /* many ties */
        std::vector<RankEntry> m = a, q = a, ref = a;
        ae_merge_sort(m.data(), n);
        ae_quick_sort(q.data(), n);
        std::sort(ref.begin(), ref.end(), [](const RankEntry& x, const RankEntry& y) {
            return x.score != y.score ? x.score > y.score : x.id < y.id;
        });
        CHECK(ordered(m)); CHECK(ordered(q));
        bool same = true;
        for (int i = 0; i < n; ++i)
            same = same && m[i].id == ref[i].id && q[i].id == ref[i].id;
        CHECK(same);
    }
    ae_merge_sort(nullptr, 5); ae_quick_sort(nullptr, 5);       /* must not crash */
}

static void testHeap() {
    std::printf("heap / priority queue\n");
    PairHeap h(1);
    MatchPair p;
    CHECK(!h.pop(p));
    std::vector<double> costs;
    for (int i = 0; i < 500; ++i) {
        double c = static_cast<double>(rng() % 1000) / 10.0;
        costs.push_back(c);
        MatchPair x; x.team_a = i; x.team_b = i + 1; x.cost = c;
        h.push(x);
    }
    CHECK(h.size() == 500);
    std::sort(costs.begin(), costs.end());
    bool ok = true;
    for (double c : costs) { ok = ok && h.pop(p) && p.cost == c; }
    CHECK(ok);
    CHECK(h.size() == 0);
    CHECK(ae_fair_score(1000, 1300, 0) == 300.0);
    CHECK(ae_fair_score(1300, 1000, 2) == 2300.0);
}

static void testHash() {
    std::printf("hash table\n");
    NameIndex t(8);
    t.put("Neon Vipers", 1);
    CHECK(t.find("neon vipers").value_or(-1) == 1);              /* case-insensitive */
    CHECK(t.find("NEON VIPERS").value_or(-1) == 1);
    CHECK(!t.find("Neon").has_value());
    t.put("NEON VIPERS", 9);                                      /* update, not insert */
    CHECK(t.count() == 1 && t.find("Neon Vipers").value_or(-1) == 9);
    for (int i = 0; i < 2000; ++i) t.put("team" + std::to_string(i), i);   /* heavy chaining */
    CHECK(t.count() == 2001);
    bool all = true;
    for (int i = 0; i < 2000; ++i) all = all && t.find("TEAM" + std::to_string(i)).value_or(-1) == i;
    CHECK(all);
    CHECK(t.remove("team5") && !t.remove("team5") && !t.find("team5").has_value());
    CHECK(t.count() == 2000);
}

static void testQueueAndList() {
    std::printf("queue + linked list\n");
    WaitQueue q;
    int v = 0;
    CHECK(!q.dequeue(v));
    for (int i = 1; i <= 5; ++i) q.enqueue(i);
    CHECK(q.remove(3) && q.remove(5) && !q.remove(42));          /* middle and tail */
    q.enqueue(6);                                                 /* tail pointer must still be valid */
    CHECK((q.toVector() == std::vector<int>{1, 2, 4, 6}));
    CHECK(q.dequeue(v) && v == 1);
    CHECK(q.size() == 3 && q.contains(6) && !q.contains(1));

    HistoryLog log;
    HistNode r{};
    r.team_a = 1; r.team_b = 2; log.append(r);
    r.team_a = 2; r.team_b = 1; log.append(r);
    r.team_a = 3; r.team_b = 1; log.append(r);
    CHECK(log.count() == 3 && log.meetings(1, 2) == 2 && log.meetings(2, 1) == 2 && log.meetings(1, 3) == 1);
    CHECK(log.meetings(2, 3) == 0);
    CHECK(log.records().size() == 3 && log.records()[2].team_a == 3);
}

static void testGraph() {
    std::printf("graph colouring / scheduling\n");
    for (int trial = 0; trial < 30; ++trial) {
        int m = 1 + static_cast<int>(rng() % 40);
        std::vector<std::pair<int, int>> pairs;
        for (int i = 0; i < m; ++i) {
            int a = static_cast<int>(rng() % 10), b = static_cast<int>(rng() % 10);
            if (a == b) b = (b + 1) % 10;
            pairs.emplace_back(a, b);
        }
        SchedulePlan plan = planSchedule(pairs, 3);
        bool valid = true;
        for (const auto& e : plan.edges) valid = valid && plan.colors[e.first] != plan.colors[e.second];
        CHECK(valid);
        /* no team plays twice in one time slot, and no slot exceeds 3 stations */
        std::map<int, std::set<int>> teamsInSlot;
        std::map<int, int> perSlot;
        bool noClash = true;
        for (int i = 0; i < m; ++i) {
            noClash = noClash && teamsInSlot[plan.slots[i]].insert(pairs[i].first).second
                              && teamsInSlot[plan.slots[i]].insert(pairs[i].second).second;
            ++perSlot[plan.slots[i]];
        }
        CHECK(noClash);
        for (auto& kv : perSlot) CHECK(kv.second <= 3);
    }
    std::vector<std::pair<int, int>> k4;                          /* K4 round robin: 6 matches */
    for (int i = 0; i < 4; ++i) for (int j = i + 1; j < 4; ++j) k4.emplace_back(i, j);
    SchedulePlan p4 = planSchedule(k4, 16);
    CHECK(p4.colorCount >= 3 && p4.colorCount <= 6);
    CHECK(planSchedule({}, 2).slotCount == 0);
}

static void testMatchmaking() {
    std::printf("fair matchmaking\n");
    std::vector<RankEntry> t = {{1, 1000}, {2, 1000}, {3, 1010}, {4, 1010}};
    HistoryLog log;
    HistNode r{}; r.team_a = 1; r.team_b = 2; log.append(r);
    r.team_a = 3; r.team_b = 4; log.append(r);
    MatchPair out[2];
    int k = ae_matchmake(t.data(), 4, log.raw(), out, 2);
    CHECK(k == 2);
    for (int i = 0; i < k; ++i) {
        bool repeat = (out[i].team_a + out[i].team_b == 3) || (out[i].team_a + out[i].team_b == 7);
        CHECK(!repeat);                                           /* repeat opponents avoided */
    }
    HistoryLog empty;
    std::vector<RankEntry> five = {{1, 1500}, {2, 1490}, {3, 1200}, {4, 1190}, {5, 900}};
    MatchPair o5[5];
    CHECK(ae_matchmake(five.data(), 5, empty.raw(), o5, 5) == 2);  /* odd team left unpaired */
    CHECK(ae_matchmake(five.data(), 1, empty.raw(), o5, 5) == 0);
}

/* ============================ Part B: application layer ============================ */

static const int ADMIN = ArenaSystem::ADMIN_ID;

static void playAll(ArenaSystem& s, int tid, int maxSteps = 5000) {
    for (int step = 0; step < maxSteps; ++step) {
        const Tournament& t = s.tournament(tid);
        if (t.status() == TStatus::Completed) return;
        const Match* next = nullptr;
        for (const Match& m : t.matches()) if (m.status() == MatchStatus::Scheduled) { next = &m; break; }
        if (!next) return;
        int a = 5 + static_cast<int>(rng() % 10), b = 5 + static_cast<int>(rng() % 10);
        if (a == b) ++a;
        s.reportResult(ADMIN, tid, next->id(), a, b);
    }
}

static int makeCup(ArenaSystem& s, Format f, int n, int capacity = 16, SortAlgo algo = SortAlgo::Merge,
                   int stations = 2) {
    s.loadSample(n);
    int tid = s.createTournament(ADMIN, "Cup", f, "Rocket League", capacity, stations, algo);
    s.registerAllFullTeams(ADMIN, tid);
    return tid;
}

static void testKnockout() {
    std::printf("knockout bracket (tree)\n");
    for (SortAlgo algo : {SortAlgo::Merge, SortAlgo::Quick})
        for (int n = 2; n <= 16; ++n) {
            ArenaSystem s;
            int tid = makeCup(s, Format::Knockout, n, 16, algo);
            s.startTournament(ADMIN, tid);
            playAll(s, tid);
            const Tournament& t = s.tournament(tid);
            CHECK(t.status() == TStatus::Completed);
            CHECK(static_cast<int>(t.matches().size()) == n - 1);       /* n-1 real matches, byes skipped */
            CHECK(t.championId(s.teams()) >= 0);
            CHECK(t.history().count() == n - 1);
        }
    /* seeding: with 8 teams the top seed meets the lowest in round 1 */
    ArenaSystem s;
    int tid = makeCup(s, Format::Knockout, 8);
    s.startTournament(ADMIN, tid);
    const Tournament& t = s.tournament(tid);
    int top = t.seeds().front().teamId, last = t.seeds().back().teamId;
    bool oneVeight = false;
    for (const Match& m : t.matches())
        if ((m.teamA() == top && m.teamB() == last) || (m.teamA() == last && m.teamB() == top)) oneVeight = true;
    CHECK(oneVeight);
    CHECK(t.seeds().front().rating >= t.seeds().back().rating);
}

static void testRoundRobinAndSwiss() {
    std::printf("round robin + swiss\n");
    for (int n = 2; n <= 10; ++n) {
        ArenaSystem s;
        int tid = makeCup(s, Format::RoundRobin, n);
        s.startTournament(ADMIN, tid);
        CHECK(static_cast<int>(s.tournament(tid).matches().size()) == n * (n - 1) / 2);
        playAll(s, tid);
        CHECK(s.tournament(tid).status() == TStatus::Completed);
        CHECK(s.tournament(tid).championId(s.teams()) >= 0);
        int wins = 0;
        for (const Standing& st : s.tournament(tid).standings(s.teams())) wins += st.wins;
        CHECK(wins == n * (n - 1) / 2);
    }
    for (int n : {2, 3, 4, 5, 7, 8, 11, 16}) {
        ArenaSystem s;
        int tid = makeCup(s, Format::Swiss, n);
        s.startTournament(ADMIN, tid);
        playAll(s, tid);
        const Tournament& t = s.tournament(tid);
        CHECK(t.status() == TStatus::Completed);
        CHECK(t.currentRound() == t.totalRounds());
        if (n >= 8) {                                               /* nobody meets twice when avoidable */
            std::set<std::pair<int, int>> seen;
            bool repeat = false;
            for (const Match& m : t.matches())
                repeat = repeat || !seen.insert({std::min(m.teamA(), m.teamB()), std::max(m.teamA(), m.teamB())}).second;
            CHECK(!repeat);
        }
        if (n % 2 == 1) CHECK(static_cast<int>(t.byes().size()) == t.totalRounds());
    }
}

static void testRegistrationQueue() {
    std::printf("registration + waitlist (queue)\n");
    ArenaSystem s;
    s.loadSample(6);
    int tid = s.createTournament(ADMIN, "Small Cup", Format::Knockout, "Rocket League", 4, 2, SortAlgo::Merge);
    auto r = s.registerAllFullTeams(ADMIN, tid);
    CHECK(r.first == 4 && r.second == 2);
    const Tournament& t = s.tournament(tid);
    CHECK(t.waitlist().size() == 2);
    int firstWaiting = t.waitlist().front();
    CHECK(s.withdrawTeam(ADMIN, tid, t.registered().front()) == firstWaiting);   /* FIFO promotion */
    CHECK(t.registered().size() == 4 && t.waitlist().size() == 1);
    int lastWaiting = t.waitlist().front();
    CHECK(s.withdrawTeam(ADMIN, tid, lastWaiting) == -1);                        /* leave from the queue */
    CHECK(t.waitlist().empty());
    s.startTournament(ADMIN, tid);
    CHECK(throwsType<InvalidOperationError>([&] { s.withdrawTeam(ADMIN, tid, t.registered().front()); }));
}

static void testExceptions() {
    std::printf("exception handling\n");
    ArenaSystem s;
    int p1 = s.registerPlayer("Aarav Sharma", "aarav@x.gg", "Aarav");
    CHECK(throwsType<DuplicateError>([&] { s.registerPlayer("Other", "other@x.gg", "aarav"); }));      /* tag, any case */
    CHECK(throwsType<DuplicateError>([&] { s.registerPlayer("Other", "AARAV@x.gg", "Other"); }));      /* email */
    CHECK(throwsType<ArenaError>([&] { s.registerPlayer("Bad", "no-at-sign", "Bad"); }));
    CHECK(throwsType<ArenaError>([&] { s.registerPlayer("", "e@x.gg", "E"); }));
    int tid = s.createTeam("Duo", "Fortnite Duos", 1000);
    CHECK(throwsType<DuplicateError>([&] { s.createTeam("duo", "Fortnite Duos", 1000); }));
    CHECK(throwsType<NotFoundError>([&] { s.createTeam("X", "No Such Game", 1000); }));
    CHECK(throwsType<ArenaError>([&] { s.createTeam("X", "Fortnite Duos", 5); }));
    s.joinTeam(tid, p1);
    CHECK(throwsType<InvalidOperationError>([&] { s.joinTeam(tid, p1); }));                          /* already in a team */
    int p2 = s.registerPlayer("B", "b@x.gg", "B"), p3 = s.registerPlayer("C", "c@x.gg", "C");
    s.joinTeam(tid, p2);
    CHECK(throwsType<InvalidOperationError>([&] { s.joinTeam(tid, p3); }));                          /* team full */
    CHECK(throwsType<NotFoundError>([&] { s.leaveTeam(tid, p3); }));
    CHECK(throwsType<NotFoundError>([&] { s.joinTeam(99, p3); }));

    CHECK(throwsType<PermissionError>([&] { s.createTournament(ArenaSystem::GUEST, "T", Format::Knockout, "Fortnite Duos", 8, 2, SortAlgo::Merge); }));
    CHECK(throwsType<PermissionError>([&] { s.createTournament(p1, "T", Format::Knockout, "Fortnite Duos", 8, 2, SortAlgo::Merge); }));
    int t1 = s.createTournament(ADMIN, "T", Format::Knockout, "Fortnite Duos", 8, 2, SortAlgo::Merge);
    CHECK(throwsType<DuplicateError>([&] { s.createTournament(ADMIN, "t", Format::Knockout, "Fortnite Duos", 8, 2, SortAlgo::Merge); }));
    CHECK(throwsType<InvalidOperationError>([&] { s.createTournament(ADMIN, "Big RR", Format::RoundRobin, "Fortnite Duos", 32, 2, SortAlgo::Merge); }));
    CHECK(throwsType<InvalidOperationError>([&] { s.createTournament(ADMIN, "One", Format::Knockout, "Fortnite Duos", 1, 2, SortAlgo::Merge); }));
    CHECK(throwsType<InvalidOperationError>([&] { s.startTournament(ADMIN, t1); }));                 /* needs 2 teams */
    int lonely = s.createTeam("Lonely", "Fortnite Duos", 1000);
    CHECK(throwsType<InvalidOperationError>([&] { s.registerTeamForTournament(ADMIN, t1, lonely); })); /* not full */
    int wrong = s.createTeam("Wrong Game", "Chess Blitz", 1000);
    CHECK(throwsType<InvalidOperationError>([&] { s.registerTeamForTournament(ADMIN, t1, wrong); }));
    s.registerTeamForTournament(ADMIN, t1, tid);
    CHECK(throwsType<DuplicateError>([&] { s.registerTeamForTournament(ADMIN, t1, tid); }));
    CHECK(throwsType<PermissionError>([&] { s.registerTeamForTournament(ArenaSystem::GUEST, t1, tid); }));

    /* match rules */
    ArenaSystem c;
    int cup = makeCup(c, Format::Knockout, 4);
    c.startTournament(ADMIN, cup);
    int mid = c.tournament(cup).matches().front().id();
    CHECK(throwsType<InvalidOperationError>([&] { c.reportResult(ADMIN, cup, mid, 7, 7); }));        /* draw */
    CHECK(throwsType<InvalidOperationError>([&] { c.reportResult(ADMIN, cup, mid, -1, 3); }));       /* negative */
    CHECK(throwsType<PermissionError>([&] { c.reportResult(ArenaSystem::GUEST, cup, mid, 3, 1); }));
    CHECK(throwsType<NotFoundError>([&] { c.reportResult(ADMIN, cup, 999, 3, 1); }));
    c.reportResult(ADMIN, cup, mid, 3, 1);
    CHECK(throwsType<InvalidOperationError>([&] { c.reportResult(ADMIN, cup, mid, 3, 1); }));        /* replay */
    int teamIn = c.tournament(cup).registered().front();
    int somePlayer = c.teams().at(teamIn).members().front();
    CHECK(throwsType<InvalidOperationError>([&] { c.leaveTeam(teamIn, somePlayer); }));              /* roster locked */
}

static void testSearch() {
    std::printf("search (hash table)\n");
    ArenaSystem s;
    s.loadSample(8);
    CHECK(s.findTeam("neon VIPERS").has_value());
    CHECK(!s.findTeam("Nobody").has_value());
    CHECK(s.findPlayer("blaze_1").has_value());
    CHECK(s.indexedTeams() == 8 && s.indexedPlayers() == 24);
    int id = *s.findTeam("Iron Foxes");
    CHECK(s.teams().at(id).name() == "Iron Foxes");
}

static void testPersistence() {
    std::printf("file handling (save / load round trip)\n");
    const std::string dir = "/tmp/arenaforge_test_data";
    for (Format f : {Format::Knockout, Format::RoundRobin, Format::Swiss})
        for (int n : {5, 8}) {
            ArenaSystem a;
            int tid = makeCup(a, f, n);
            a.startTournament(ADMIN, tid);
            for (int i = 0; i < 3; ++i) {                       /* play a few matches, leave the rest */
                for (const Match& m : a.tournament(tid).matches())
                    if (m.status() == MatchStatus::Scheduled) { a.reportResult(ADMIN, tid, m.id(), 9, 4); break; }
            }
            std::string before = buildSnapshot(a, SortAlgo::Merge);
            a.save(dir);
            ArenaSystem b;
            b.load(dir);
            std::string after = buildSnapshot(b, SortAlgo::Merge);
            CHECK(before == after);                             /* every field survives the round trip */
            playAll(b, tid);                                    /* ...and the loaded tournament still works */
            CHECK(b.tournament(tid).status() == TStatus::Completed);
        }
    ArenaSystem fresh;
    CHECK(throwsType<NotFoundError>([&] { fresh.load("/tmp/arenaforge_missing_dir_xyz"); }));
    CHECK(fresh.users().size() == 1);                           /* failed load leaves a clean workspace */

    ArenaSystem odd;                                            /* awkward characters survive */
    odd.registerPlayer("Name|with|bars", "pipe@x.gg", "Tag%25|x");
    odd.createTeam("Team|One\nLine", "Chess Blitz", 1234.5);
    odd.save(dir);
    ArenaSystem back;
    back.load(dir);
    CHECK(back.findTeam("Team|One\nLine").has_value());
    CHECK(back.findPlayer("Tag%25|x").has_value());
}

static void testSnapshotJson() {
    std::printf("json snapshot\n");
    ArenaSystem s;
    int tid = makeCup(s, Format::Swiss, 6);
    s.startTournament(ADMIN, tid);
    std::string js = buildSnapshot(s, SortAlgo::Quick);
    CHECK(js.find("\"matchmaking\":{") != std::string::npos);
    CHECK(js.find("\"groups\":[") != std::string::npos);
    int depth = 0; bool inStr = false, ok = true;                /* brackets balance outside strings */
    for (std::size_t i = 0; i < js.size(); ++i) {
        char c = js[i];
        if (inStr) { if (c == '\\') ++i; else if (c == '"') inStr = false; continue; }
        if (c == '"') inStr = true;
        else if (c == '{' || c == '[') ++depth;
        else if (c == '}' || c == ']') { --depth; ok = ok && depth >= 0; }
    }
    CHECK(ok && depth == 0 && !inStr);
}

int main() {
    std::printf("== Part A: C DSA engine ==\n");
    testSorting();
    testHeap();
    testHash();
    testQueueAndList();
    testGraph();
    testMatchmaking();
    std::printf("== Part B: C++ application layer ==\n");
    testKnockout();
    testRoundRobinAndSwiss();
    testRegistrationQueue();
    testExceptions();
    testSearch();
    testPersistence();
    testSnapshotJson();
    std::printf("\n%d checks, %d failed\n", g_checks, g_failed);
    return g_failed ? 1 : 0;
}
