/*
 ArenaForge - C++ application layer
 Web API: a thin C-callable surface over ArenaSystem, compiled to WebAssembly.
 Every function returns a JSON string: {"ok":true,...} or {"ok":false,"type":...,"error":...}.
 Exceptions never cross the boundary; they become error objects (exception handling).
 */
#include "arena_system.hpp"
#include "json.hpp"
#include "snapshot.hpp"

#include <algorithm>
#include <string>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define AA_EXPORT extern "C" EMSCRIPTEN_KEEPALIVE
#else
#define AA_EXPORT extern "C"
#endif

using namespace arena;

namespace {

const char* kDataDir = "/data";                      /* virtual file system folder holding the .dat files */

ArenaSystem* g_sys = nullptr;
std::string  g_out;

ArenaSystem& sys() {
    if (!g_sys) g_sys = new ArenaSystem();
    return *g_sys;
}

std::string okJson(const std::string& message, long long id = 0) {
    Json j;
    j.beginObject().kvb("ok", true).kvs("message", message).kvi("id", id).end();
    return j.result();
}

std::string failJson(const char* type, const std::string& message) {
    Json j;
    j.beginObject().kvb("ok", false).kvs("type", type).kvs("error", message).end();
    return j.result();
}

template <class F>
const char* guarded(F&& f) {
    try { g_out = f(); }
    catch (const DuplicateError& e)         { g_out = failJson("DuplicateError", e.what()); }
    catch (const NotFoundError& e)          { g_out = failJson("NotFoundError", e.what()); }
    catch (const InvalidOperationError& e)  { g_out = failJson("InvalidOperationError", e.what()); }
    catch (const PermissionError& e)        { g_out = failJson("PermissionError", e.what()); }
    catch (const ArenaError& e)             { g_out = failJson("ArenaError", e.what()); }
    catch (const std::exception& e)         { g_out = failJson("Error", e.what()); }
    catch (...)                             { g_out = failJson("Error", "Unknown error"); }
    return g_out.c_str();
}

std::string teamName(int id) {
    auto it = sys().teams().find(id);
    return it == sys().teams().end() ? "#" + std::to_string(id) : it->second.name();
}

SortAlgo algoFrom(int v) { return v == 1 ? SortAlgo::Quick : SortAlgo::Merge; }

} // namespace

AA_EXPORT const char* aa_snapshot(int leaderboardAlgo) {
    return guarded([&] { return buildSnapshot(sys(), algoFrom(leaderboardAlgo)); });
}

AA_EXPORT const char* aa_reset() {
    return guarded([&] { sys().reset(); return okJson("Workspace cleared"); });
}

AA_EXPORT const char* aa_load_sample(int teams) {
    return guarded([&] {
        sys().loadSample(teams);
        return okJson("Loaded " + std::to_string(teams) + " sample teams");
    });
}

AA_EXPORT const char* aa_register_player(const char* name, const char* email, const char* tag) {
    return guarded([&] {
        int id = sys().registerPlayer(name, email, tag);
        return okJson(std::string("Registered ") + tag, id);
    });
}

AA_EXPORT const char* aa_add_game(const char* name, int ppt) {
    return guarded([&] {
        sys().addGame(name, ppt);
        return okJson(std::string("Added game ") + name);
    });
}

AA_EXPORT const char* aa_create_team(const char* name, const char* game, double rating) {
    return guarded([&] {
        int id = sys().createTeam(name, game, rating);
        return okJson(std::string("Created team ") + name, id);
    });
}

AA_EXPORT const char* aa_join_team(int teamId, int playerId) {
    return guarded([&] {
        sys().joinTeam(teamId, playerId);
        return okJson("Player added to " + teamName(teamId));
    });
}

AA_EXPORT const char* aa_leave_team(int teamId, int playerId) {
    return guarded([&] {
        sys().leaveTeam(teamId, playerId);
        return okJson("Player removed from " + teamName(teamId));
    });
}

AA_EXPORT const char* aa_create_tournament(int actor, const char* name, int format, const char* game,
                                           int capacity, int stations, int algo) {
    return guarded([&] {
        if (format < 0 || format > 2) throw InvalidOperationError("Unknown tournament format");
        int id = sys().createTournament(actor, name, static_cast<Format>(format), game, capacity, stations,
                                        algoFrom(algo));
        return okJson(std::string("Created ") + name, id);
    });
}

AA_EXPORT const char* aa_register_team(int actor, int tid, int teamId) {
    return guarded([&] {
        RegResult r = sys().registerTeamForTournament(actor, tid, teamId);
        if (r == RegResult::Registered) return okJson(teamName(teamId) + " is registered", 0);
        return okJson(teamName(teamId) + " joined the waitlist (tournament is full)", 1);
    });
}

AA_EXPORT const char* aa_register_all(int actor, int tid) {
    return guarded([&] {
        std::pair<int, int> r = sys().registerAllFullTeams(actor, tid);
        if (r.first + r.second == 0) return okJson("No eligible teams to register");
        std::string msg = "Registered " + std::to_string(r.first) + " team(s)";
        if (r.second) msg += ", " + std::to_string(r.second) + " on the waitlist";
        return okJson(msg);
    });
}

AA_EXPORT const char* aa_withdraw_team(int actor, int tid, int teamId) {
    return guarded([&] {
        int promoted = sys().withdrawTeam(actor, tid, teamId);
        std::string msg = teamName(teamId) + " withdrew";
        if (promoted >= 0) msg += "; " + teamName(promoted) + " moved up from the waitlist";
        return okJson(msg);
    });
}

AA_EXPORT const char* aa_start_tournament(int actor, int tid) {
    return guarded([&] {
        sys().startTournament(actor, tid);
        return okJson(sys().tournament(tid).name() + " has started");
    });
}

AA_EXPORT const char* aa_report_result(int actor, int tid, int matchId, int scoreA, int scoreB) {
    return guarded([&] {
        sys().reportResult(actor, tid, matchId, scoreA, scoreB);
        const Tournament& t = sys().tournament(tid);
        const Match* m = t.findMatch(matchId);
        std::string msg = teamName(m->winnerId()) + " won " + std::to_string(std::max(m->scoreA(), m->scoreB())) +
                          "\xE2\x80\x93" + std::to_string(std::min(m->scoreA(), m->scoreB()));   /* en dash */
        if (t.status() == TStatus::Completed) msg += ". " + t.name() + " is complete";
        return okJson(msg);
    });
}

AA_EXPORT const char* aa_search_team(const char* name) {
    return guarded([&] {
        std::optional<int> id = sys().findTeam(name);            /* C hash table lookup */
        Json j;
        j.beginObject().kvb("ok", true).kvb("found", id.has_value()).kvi("id", id.value_or(-1)).end();
        return j.result();
    });
}

AA_EXPORT const char* aa_search_player(const char* tag) {
    return guarded([&] {
        std::optional<int> id = sys().findPlayer(tag);           /* C hash table lookup */
        Json j;
        j.beginObject().kvb("ok", true).kvb("found", id.has_value()).kvi("id", id.value_or(-1)).end();
        return j.result();
    });
}

/* file handling: write / read the seven .dat files in the virtual folder /data */
AA_EXPORT const char* aa_save() {
    return guarded([&] { sys().save(kDataDir); return okJson("Saved to " + std::string(kDataDir)); });
}

AA_EXPORT const char* aa_load() {
    return guarded([&] { sys().load(kDataDir); return okJson("Loaded saved workspace"); });
}
