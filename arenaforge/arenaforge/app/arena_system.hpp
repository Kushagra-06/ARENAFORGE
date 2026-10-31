/*
 ArenaForge - C++ application layer
 ArenaSystem: the application controller.

   users (User -> Player / Admin)   teams   games   tournaments
   hash-table indexes (C)           leaderboard (C sort)
   file handling: Players.dat, Teams.dat, Tournaments.dat, Matches.dat,
                  Results.dat, Rankings.dat, History.dat
 */
#pragma once
#include "engine_raii.hpp"
#include "models.hpp"
#include "tournament.hpp"

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace arena {

class ArenaSystem {
public:
    static constexpr int ADMIN_ID = 1;
    static constexpr int GUEST = 0;                  /* "not logged in as admin" */

    ArenaSystem() { reset(); }
    void reset();                                    /* empty workspace + default admin/games */

    /* ----- games ----- */
    const std::vector<Game>& games() const { return games_; }
    void addGame(const std::string& name, int playersPerTeam);

    /* ----- users ----- */
    int registerPlayer(const std::string& name, const std::string& email, const std::string& gamerTag);
    const std::map<int, std::unique_ptr<User>>& users() const { return users_; }

    /* ----- teams ----- */
    int createTeam(const std::string& name, const std::string& gameName, double rating = 1000.0);
    void joinTeam(int teamId, int playerId);
    void leaveTeam(int teamId, int playerId);
    const TeamBook& teams() const { return teams_; }

    /* ----- tournaments (administrators only) ----- */
    int createTournament(int actor, const std::string& name, Format format, const std::string& gameName,
                         int capacity, int stations, SortAlgo algo);
    RegResult registerTeamForTournament(int actor, int tournamentId, int teamId);
    std::pair<int, int> registerAllFullTeams(int actor, int tournamentId);   /* {registered, waitlisted} */
    int withdrawTeam(int actor, int tournamentId, int teamId);
    void startTournament(int actor, int tournamentId);
    void reportResult(int actor, int tournamentId, int matchId, int scoreA, int scoreB);
    const std::vector<std::unique_ptr<Tournament>>& tournaments() const { return tournaments_; }
    const Tournament& tournament(int id) const;

    /* ----- search (C hash tables) ----- */
    std::optional<int> findTeam(const std::string& name) const { return teamIndex_.find(name); }
    std::optional<int> findPlayer(const std::string& gamerTag) const { return tagIndex_.find(gamerTag); }
    int indexedTeams() const { return teamIndex_.count(); }
    int indexedPlayers() const { return tagIndex_.count(); }

    /* ----- ranking ----- */
    std::vector<RankEntry> leaderboard(SortAlgo algo) const;

    /* ----- demo data ----- */
    void loadSample(int teamCount);

    /* ----- file handling ----- */
    void save(const std::string& dir) const;
    void load(const std::string& dir);

private:
    void requireAdmin(int actor, const std::string& action) const;
    Team& teamRef(int id);
    Player& playerRef(int id);
    Tournament& tourRef(int id);
    const Game& gameByName(const std::string& name) const;
    bool teamLocked(int teamId) const;

    std::vector<Game> games_;
    std::map<int, std::unique_ptr<User>> users_;
    TeamBook teams_;
    std::vector<std::unique_ptr<Tournament>> tournaments_;
    NameIndex teamIndex_, tagIndex_, emailIndex_;
    int nextUserId_ = 1, nextTeamId_ = 1, nextTournamentId_ = 1;
};

} // namespace arena
