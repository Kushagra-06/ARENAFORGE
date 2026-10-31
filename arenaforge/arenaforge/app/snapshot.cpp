/*
 ArenaForge - C++ application layer
 Snapshot builder: reads the application state (and asks the C engine for the
 matchmaking heap and the scheduling graph) and writes it out as JSON.
 */
#include "snapshot.hpp"

#include "json.hpp"

#include <algorithm>
#include <set>
#include <sstream>

namespace arena {

namespace {

const char* statusKey(TStatus s) {
    switch (s) {
        case TStatus::Registration: return "registration";
        case TStatus::InProgress:   return "inprogress";
        case TStatus::Completed:    return "completed";
    }
    return "registration";
}

const char* formatKey(Format f) {
    switch (f) {
        case Format::Knockout:   return "knockout";
        case Format::RoundRobin: return "roundrobin";
        case Format::Swiss:      return "swiss";
    }
    return "knockout";
}

void writeNode(Json& j, const BracketNode* n) {
    j.beginObject();
    j.kvi("id", n->node_id).kvi("round", n->round).kvi("team", n->team_id);
    if (n->left)  { j.key("l"); writeNode(j, n->left); }
    if (n->right) { j.key("r"); writeNode(j, n->right); }
    j.end();
}

void writePair(Json& j, const MatchPair& p, const TeamBook& teams, const Tournament& t) {
    double ra = teams.at(p.team_a).rating(), rb = teams.at(p.team_b).rating();
    j.beginObject();
    j.kvi("a", p.team_a).kvi("b", p.team_b).kvd("cost", p.cost)
     .kvd("gap", ra > rb ? ra - rb : rb - ra)
     .kvi("meetings", t.history().meetings(p.team_a, p.team_b));
    j.end();
}

/* Heap view: every pairing is pushed into the C min-heap keyed by Fair Matchmaking
   Score; we pop the cheapest few, then ask the engine for the final pairing. */
void writeMatchmaking(Json& j, const Tournament& t, const TeamBook& teams) {
    j.key("matchmaking");
    const std::vector<int>& reg = t.registered();
    if (reg.size() < 2) { j.nul(); return; }

    std::vector<RankEntry> live;
    for (int id : reg) live.push_back({id, teams.at(id).rating()});
    sortRanking(live, t.sortAlgo());
    const int n = static_cast<int>(live.size());

    PairHeap heap(n * (n - 1) / 2);
    for (int a = 0; a < n; ++a)
        for (int b = a + 1; b < n; ++b) {
            MatchPair p;
            p.team_a = live[static_cast<std::size_t>(a)].id;
            p.team_b = live[static_cast<std::size_t>(b)].id;
            p.cost = ae_fair_score(live[static_cast<std::size_t>(a)].score, live[static_cast<std::size_t>(b)].score,
                                   t.history().meetings(p.team_a, p.team_b));
            heap.push(p);
        }

    j.beginObject();
    j.kvi("pairsInHeap", heap.size());
    j.key("candidates").beginArray();
    MatchPair p;
    for (int k = 0; k < 12 && heap.pop(p); ++k) writePair(j, p, teams, t);
    j.end();

    std::vector<MatchPair> out(static_cast<std::size_t>(n / 2));
    int k = ae_matchmake(live.data(), n, t.history().raw(), out.data(), static_cast<int>(out.size()));
    std::set<int> used;
    j.key("pairing").beginArray();
    for (int i = 0; i < k; ++i) {
        writePair(j, out[static_cast<std::size_t>(i)], teams, t);
        used.insert(out[static_cast<std::size_t>(i)].team_a);
        used.insert(out[static_cast<std::size_t>(i)].team_b);
    }
    j.end();
    int unpaired = -1;
    for (const RankEntry& e : live) if (!used.count(e.id)) unpaired = e.id;
    j.kvi("unpaired", unpaired);
    j.end();
}

/* Scheduling view: one conflict graph per round (matches that share a team clash). */
void writeGroups(Json& j, const Tournament& t) {
    j.key("groups").beginArray();
    std::set<int> rounds;
    for (const Match& m : t.matches()) rounds.insert(m.round());
    for (int r : rounds) {
        std::vector<const Match*> ms;
        std::vector<std::pair<int, int>> pairs;
        int maxSlot = 0;
        for (const Match& m : t.matches())
            if (m.round() == r) { ms.push_back(&m); pairs.emplace_back(m.teamA(), m.teamB()); maxSlot = std::max(maxSlot, m.slot()); }
        SchedulePlan plan = planSchedule(pairs, t.stations());
        j.beginObject();
        j.kvi("round", r).kvs("name", t.roundName(r));
        j.key("matchIds").beginArray();
        for (const Match* m : ms) j.i(m->id());
        j.end();
        j.key("colors").beginArray();
        for (int c : plan.colors) j.i(c);
        j.end();
        j.kvi("colorCount", plan.colorCount).kvi("slotCount", maxSlot + 1);
        j.key("edges").beginArray();
        for (const auto& e : plan.edges) j.beginArray().i(e.first).i(e.second).end();
        j.end();
        j.end();
    }
    j.end();
}

void writeTournament(Json& j, const Tournament& t, const TeamBook& teams) {
    j.beginObject();
    j.kvi("id", t.id()).kvs("name", t.name()).kvs("format", formatKey(t.format()))
     .kvs("formatName", t.formatName()).kvs("game", t.game().name()).kvi("ppt", t.game().playersPerTeam())
     .kvi("capacity", t.capacity()).kvi("stations", t.stations())
     .kvs("algo", t.sortAlgo() == SortAlgo::Merge ? "merge" : "quick")
     .kvs("status", statusKey(t.status())).kvi("createdBy", t.createdBy())
     .kvi("round", t.currentRound()).kvi("totalRounds", t.totalRounds())
     .kvi("champion", t.championId(teams));

    j.key("registered").beginArray();
    for (int id : t.registered()) j.i(id);
    j.end();
    j.key("waitlist").beginArray();
    for (int id : t.waitlist()) j.i(id);
    j.end();
    j.key("seeds").beginArray();
    for (const Seed& s : t.seeds()) j.beginObject().kvi("teamId", s.teamId).kvd("rating", s.rating).end();
    j.end();
    j.key("byes").beginArray();
    for (const ByeRecord& b : t.byes()) j.beginObject().kvi("round", b.round).kvi("teamId", b.teamId).end();
    j.end();

    j.key("matches").beginArray();
    for (const Match& m : t.matches()) {
        bool done = m.status() == MatchStatus::Completed;
        j.beginObject();
        j.kvi("id", m.id()).kvi("round", m.round()).kvs("roundName", t.roundName(m.round()))
         .kvi("a", m.teamA()).kvi("b", m.teamB()).kvi("slot", m.slot()).kvi("ref", m.ref())
         .kvs("status", done ? "completed" : "scheduled")
         .kvi("sa", m.scoreA()).kvi("sb", m.scoreB()).kvi("winner", done ? m.winnerId() : AE_TBD);
        j.end();
    }
    j.end();

    j.key("standings").beginArray();
    if (t.status() != TStatus::Registration)
        for (const Standing& s : t.standings(teams))
            j.beginObject().kvi("teamId", s.teamId).kvi("played", s.played).kvi("wins", s.wins)
             .kvi("losses", s.losses).kvi("diff", s.diff).end();
    j.end();

    j.key("bracket");
    const KnockoutTournament* ko = dynamic_cast<const KnockoutTournament*>(&t);
    if (ko && ko->root()) writeNode(j, ko->root()); else j.nul();

    writeGroups(j, t);

    j.key("history").beginArray();
    for (const HistNode& h : t.history().records())
        j.beginObject().kvi("matchId", h.match_id).kvi("round", h.round).kvi("a", h.team_a).kvi("b", h.team_b)
         .kvi("sa", h.score_a).kvi("sb", h.score_b).kvi("winner", h.winner).end();
    j.end();

    if (t.format() == Format::Swiss) writeMatchmaking(j, t, teams);
    j.end();
}

} // namespace

std::string buildSnapshot(const ArenaSystem& sys, SortAlgo leaderboardAlgo) {
    const TeamBook& teams = sys.teams();
    Json j;
    j.beginObject().kvb("ok", true);

    j.key("games").beginArray();
    for (const Game& g : sys.games()) j.beginObject().kvs("name", g.name()).kvi("ppt", g.playersPerTeam()).end();
    j.end();

    j.key("users").beginArray();
    for (const auto& kv : sys.users()) {
        const User& u = *kv.second;
        const Player* p = dynamic_cast<const Player*>(&u);
        std::ostringstream desc;
        u.describe(desc);                                   /* virtual call: Player / Admin */
        j.beginObject().kvi("id", u.id()).kvs("role", u.role()).kvs("name", u.name()).kvs("email", u.email())
         .kvs("tag", p ? p->gamerTag() : "").kvi("teamId", p ? p->teamId() : -1).kvs("desc", desc.str()).end();
    }
    j.end();

    j.key("teams").beginArray();
    for (const auto& kv : teams) {
        const Team& t = kv.second;
        int ppt = 0;
        for (const Game& g : sys.games()) if (g.name() == t.gameName()) ppt = g.playersPerTeam();
        j.beginObject().kvi("id", t.id()).kvs("name", t.name()).kvs("game", t.gameName()).kvi("ppt", ppt);
        j.key("members").beginArray();
        for (int m : t.members()) j.i(m);
        j.end();
        j.kvb("full", t.isFull()).kvd("rating", t.rating()).kvi("wins", t.wins()).kvi("losses", t.losses());
        j.end();
    }
    j.end();

    j.key("tournaments").beginArray();
    for (const auto& t : sys.tournaments()) writeTournament(j, *t, teams);
    j.end();

    j.key("leaderboard").beginArray();
    int rank = 1;
    for (const RankEntry& e : sys.leaderboard(leaderboardAlgo)) {
        const Team& t = teams.at(e.id);
        j.beginObject().kvi("rank", rank++).kvi("id", t.id()).kvs("name", t.name()).kvd("rating", t.rating())
         .kvi("wins", t.wins()).kvi("losses", t.losses()).end();
    }
    j.end();

    j.key("engine").beginObject()
     .kvi("hashTeams", sys.indexedTeams()).kvi("hashPlayers", sys.indexedPlayers())
     .kvs("leaderboardAlgo", leaderboardAlgo == SortAlgo::Merge ? "merge" : "quick")
     .end();

    j.end();
    return j.result();
}

} // namespace arena
