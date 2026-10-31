/*
 ArenaForge - C++ application layer
 Tournament (abstract)  ->  KnockoutTournament, RoundRobinTournament, SwissTournament
 Leaderboard, Elo rating

 Polymorphism: the base class owns registration, seeding, result handling and
 history (template-method pattern). Each format only decides how matches are
 created and when it is finished.
 */
#pragma once
#include "engine_raii.hpp"
#include "models.hpp"

#include <cmath>
#include <map>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace arena {

using TeamBook = std::map<int, Team>;

enum class Format  { Knockout = 0, RoundRobin = 1, Swiss = 2 };
enum class TStatus { Registration = 0, InProgress = 1, Completed = 2 };
enum class RegResult { Registered, Waitlisted };

struct Seed      { int teamId; double rating; };
struct ByeRecord { int round;  int teamId; };
struct Standing {
    int teamId = 0;
    int played = 0, wins = 0, losses = 0, diff = 0;
    double key = 0;                  /* value handed to the C sort */
};

/* Elo: the rating update applied after every completed match */
struct Elo {
    static constexpr double K = 32.0;
    static double expected(double ra, double rb) { return 1.0 / (1.0 + std::pow(10.0, (rb - ra) / 400.0)); }
    static void apply(Team& winner, Team& loser) {
        double delta = K * (1.0 - expected(winner.rating(), loser.rating()));
        winner.setRating(winner.rating() + delta);
        loser.setRating(loser.rating() - delta);
    }
};

class Tournament {
public:
    Tournament(int id, std::string name, const Game& game, int capacity, int stations,
               SortAlgo algo, int createdBy);
    virtual ~Tournament() = default;
    Tournament(const Tournament&) = delete;
    Tournament& operator=(const Tournament&) = delete;

    /* ----- format specific (polymorphic) ----- */
    virtual Format format() const = 0;
    virtual std::string formatName() const = 0;
    virtual std::string roundName(int round) const { return "Round " + std::to_string(round); }
    virtual int totalRounds() const = 0;
    virtual int currentRound() const = 0;
    virtual bool isFinished() const = 0;
    virtual int championId(const TeamBook& teams) const;       /* AE_TBD until finished */

    /* ----- common ----- */
    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const Game& game() const { return game_; }
    int capacity() const { return capacity_; }
    int stations() const { return stations_; }
    SortAlgo sortAlgo() const { return algo_; }
    int createdBy() const { return createdBy_; }
    TStatus status() const { return status_; }

    const std::vector<int>& registered() const { return registered_; }
    std::vector<int> waitlist() const { return waitlist_.toVector(); }
    bool isEntered(int teamId) const;
    const std::vector<Seed>& seeds() const { return seeds_; }
    const std::vector<Match>& matches() const { return matches_; }
    const std::vector<ByeRecord>& byes() const { return byes_; }
    const HistoryLog& history() const { return history_; }
    const std::vector<int>& completionOrder() const { return order_; }
    const Match* findMatch(int matchId) const;
    std::vector<Standing> standings(const TeamBook& teams) const;

    RegResult registerTeam(const Team& team);                  /* queue when full */
    int withdrawTeam(int teamId);                              /* returns promoted team or -1 */
    void start(TeamBook& teams);                               /* seed (C sort) + create matches */
    void reportResult(int matchId, int scoreA, int scoreB, TeamBook& teams);

    /* ----- used by file loading ----- */
    void restoreRegistered(int teamId) { registered_.push_back(teamId); }
    void restoreWaitlisted(int teamId) { waitlist_.enqueue(teamId); }
    void restoreSeed(int teamId, double rating) { seeds_.push_back({teamId, rating}); }
    void restoreBye(int round, int teamId) { byes_.push_back({round, teamId}); }
    void restoreMatch(int id, int round, int a, int b, int slot, int ref);
    void restoreResult(int matchId, int scoreA, int scoreB);
    void restoreHistory(int matchId);
    void restoreStatus(TStatus s);

protected:
    virtual void startImpl(TeamBook& teams) = 0;
    virtual void onMatchCompleted(const Match& m, TeamBook& teams) = 0;
    virtual void rebuild() {}                                  /* re-derive state after loading */
    virtual int bonusWins(int /*teamId*/) const { return 0; }
    virtual double standingKey(const Standing& s, double /*rating*/) const {
        return s.wins * 1e6 + (s.diff + 50000.0);
    }

    /* creates one batch of matches; slots come from the C graph colouring */
    std::vector<int> addBatch(int round, const std::vector<std::pair<int, int>>& pairs,
                              const std::vector<int>& refs);
    void addBye(int round, int teamId) { byes_.push_back({round, teamId}); }
    bool hadBye(int teamId) const;
    bool roundComplete(int round) const;
    const Match* findByRef(int ref) const;

private:
    Match* mutableMatch(int matchId);

    int id_;
    std::string name_;
    Game game_;
    int capacity_, stations_;
    SortAlgo algo_;
    int createdBy_;
    TStatus status_ = TStatus::Registration;

    std::vector<int> registered_;
    WaitQueue waitlist_;                 /* C queue */
    std::vector<Seed> seeds_;
    std::vector<Match> matches_;
    std::vector<ByeRecord> byes_;
    HistoryLog history_;                 /* C linked list */
    std::vector<int> order_;             /* match ids in completion order */
    int nextMatchId_ = 1;
};

/* ---------------- Knockout: C bracket tree ---------------- */
class KnockoutTournament : public Tournament {
public:
    using Tournament::Tournament;
    Format format() const override { return Format::Knockout; }
    std::string formatName() const override { return "Single elimination"; }
    std::string roundName(int round) const override;
    int totalRounds() const override { return bracket_.root() ? bracket_.root()->round : 0; }
    int currentRound() const override;
    bool isFinished() const override { return bracket_.root() && bracket_.champion() >= 0; }
    int championId(const TeamBook&) const override { return bracket_.champion(); }
    const BracketNode* root() const { return bracket_.root(); }

protected:
    void startImpl(TeamBook& teams) override;
    void onMatchCompleted(const Match& m, TeamBook& teams) override;
    void rebuild() override;

private:
    void spawnReadyMatches();
    BracketTree bracket_;
};

/* ---------------- Round robin: everyone plays everyone ---------------- */
class RoundRobinTournament : public Tournament {
public:
    using Tournament::Tournament;
    Format format() const override { return Format::RoundRobin; }
    std::string formatName() const override { return "Round robin"; }
    int totalRounds() const override { return matches().empty() ? 0 : 1; }
    int currentRound() const override { return matches().empty() ? 0 : 1; }
    bool isFinished() const override { return !matches().empty() && roundComplete(1); }

protected:
    void startImpl(TeamBook& teams) override;
    void onMatchCompleted(const Match&, TeamBook&) override {}
};

/* ---------------- Swiss: heap-based smart matchmaking each round ---------------- */
class SwissTournament : public Tournament {
public:
    using Tournament::Tournament;
    Format format() const override { return Format::Swiss; }
    std::string formatName() const override { return "Swiss (smart matchmaking)"; }
    std::string roundName(int round) const override {
        return "Round " + std::to_string(round) + " of " + std::to_string(total_);
    }
    int totalRounds() const override { return total_; }
    int currentRound() const override { return round_; }
    bool isFinished() const override { return round_ > 0 && round_ >= total_ && roundComplete(round_); }

protected:
    void startImpl(TeamBook& teams) override;
    void onMatchCompleted(const Match& m, TeamBook& teams) override;
    void rebuild() override;
    int bonusWins(int teamId) const override;
    double standingKey(const Standing& s, double rating) const override { return s.wins * 1e6 + rating; }

private:
    static int roundsFor(int teams);
    void createRound(TeamBook& teams);
    int round_ = 0, total_ = 0;
};

std::unique_ptr<Tournament> makeTournament(Format f, int id, std::string name, const Game& game,
                                           int capacity, int stations, SortAlgo algo, int createdBy);

/* ---------------- Leaderboard: all teams ranked by the C sort ---------------- */
class Leaderboard {
public:
    void rebuild(const TeamBook& teams, SortAlgo algo) {
        entries_.clear();
        for (const auto& kv : teams) entries_.push_back({kv.first, kv.second.rating()});
        sortRanking(entries_, algo);
    }
    const std::vector<RankEntry>& entries() const { return entries_; }

private:
    std::vector<RankEntry> entries_;
};

} // namespace arena
