/*
ArenaForge - C++ application layer
User (abstract) -> Player, Admin      (inheritance + abstraction)
Game, Team, Match                     (encapsulation)
Exception hierarchy for invalid operations
 */
#pragma once
#include <algorithm>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace arena {
// Exceptions
class ArenaError : public std::runtime_error {
public:
    explicit ArenaError(const std::string& msg) : std::runtime_error(msg) {}
};
class DuplicateError : public ArenaError {
public:
    explicit DuplicateError(const std::string& m) : ArenaError(m) {}
};
class NotFoundError : public ArenaError {
public:
    explicit NotFoundError(const std::string& m) : ArenaError(m) {}
};
class InvalidOperationError : public ArenaError {
public:
    explicit InvalidOperationError(const std::string& m) : ArenaError(m) {}
};
class PermissionError : public ArenaError {
public:
    explicit PermissionError(const std::string& m) : ArenaError(m) {}
};
// User (abstract base)  ->  Player, Admin
class User {
public:
    User(int id, std::string name, std::string email)
        : id_(id), name_(std::move(name)), email_(std::move(email)) {
        if (name_.empty()) throw ArenaError("User name cannot be empty");
        if (email_.find('@') == std::string::npos)
            throw ArenaError("Invalid email address: " + email_);
    }
    virtual ~User() = default;

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& email() const { return email_; }

    virtual std::string role() const = 0;// pure virtual -> abstract class
    virtual void describe(std::ostream& os) const {
        os << role() << " #" << id_ << " " << name_ << " <" << email_ << ">";
    }

private:// encapsulated state
    int id_;
    std::string name_;
    std::string email_;
};

class Player : public User {
public:
    Player(int id, std::string name, std::string email, std::string gamerTag)
        : User(id, std::move(name), std::move(email)), gamerTag_(std::move(gamerTag)) {}

    std::string role() const override { return "Player"; }
    void describe(std::ostream& os) const override {
        User::describe(os);
        os << " [" << gamerTag_ << "]";
    }

    const std::string& gamerTag() const { return gamerTag_; }
    bool hasTeam() const { return teamId_ != -1; }
    int teamId() const { return teamId_; }
    void assignTeam(int teamId) { teamId_ = teamId; }
    void leaveTeam() { teamId_ = -1; }

private:
    std::string gamerTag_;
    int teamId_ = -1;
};

class Admin : public User {
public:
    Admin(int id, std::string name, std::string email, int accessLevel = 1)
        : User(id, std::move(name), std::move(email)), accessLevel_(accessLevel) {}

    std::string role() const override { return "Admin"; }
    bool canManageTournaments() const { return accessLevel_ >= 1; }

private:
    int accessLevel_;
};
// Game
class Game {
public:
    Game(std::string name, int playersPerTeam)
        : name_(std::move(name)), playersPerTeam_(playersPerTeam) {
        if (name_.empty() || playersPerTeam_ < 1)
            throw ArenaError("Game needs a name and at least 1 player per team");
    }
    const std::string& name() const { return name_; }
    int playersPerTeam() const { return playersPerTeam_; }

private:
    std::string name_;
    int playersPerTeam_;
};
//Team
class Team {
public:
    Team(int id, std::string name, const Game& game)
        : id_(id), name_(std::move(name)), game_(game) {
        if (name_.empty()) throw ArenaError("Team name cannot be empty");
    }

    int id() const { return id_; }
    const std::string& name() const { return name_; }
    const std::string& gameName() const { return game_.name(); }
    const std::vector<int>& members() const { return members_; }
    bool isFull() const { return static_cast<int>(members_.size()) == game_.playersPerTeam(); }

    void addPlayer(Player& p) {
        if (isFull()) throw InvalidOperationError("Team " + name_ + " is already full");
        if (p.hasTeam()) throw InvalidOperationError(p.name() + " already belongs to a team");
        members_.push_back(p.id());
        p.assignTeam(id_);
    }
    void removePlayer(Player& p) {
        auto it = std::find(members_.begin(), members_.end(), p.id());
        if (it == members_.end()) throw NotFoundError(p.name() + " is not in team " + name_);
        members_.erase(it);
        p.leaveTeam();
    }

    double rating() const { return rating_; }
    void setRating(double r) { rating_ = r; }
    int wins() const { return wins_; }
    int losses() const { return losses_; }
    void recordWin() { ++wins_; }
    void recordLoss() { ++losses_; }

private:
    int id_;
    std::string name_;
    Game game_;
    std::vector<int> members_;
    double rating_ = 1000.0;
    int wins_ = 0, losses_ = 0;
};
// Match
enum class MatchStatus { Scheduled, Completed };

class Match {
public:
    Match(int id, int round, int teamA, int teamB, int slot, int ref)
        : id_(id), round_(round), teamA_(teamA), teamB_(teamB), slot_(slot), ref_(ref) {}

    void play(int scoreA, int scoreB) {
        if (status_ == MatchStatus::Completed)
            throw InvalidOperationError("Match " + std::to_string(id_) + " was already played");
        if (scoreA < 0 || scoreB < 0) throw InvalidOperationError("Scores cannot be negative");
        if (scoreA == scoreB) throw InvalidOperationError("Draws are not allowed in this system");
        scoreA_ = scoreA;
        scoreB_ = scoreB;
        status_ = MatchStatus::Completed;
    }

    int id() const { return id_; }
    int round() const { return round_; }
    int teamA() const { return teamA_; }
    int teamB() const { return teamB_; }
    int scoreA() const { return scoreA_; }
    int scoreB() const { return scoreB_; }
    int slot() const { return slot_; }// time slot chosen by graph colouring
    int ref() const { return ref_; }// format-specific reference (e.g. bracket node)
    MatchStatus status() const { return status_; }
    int winnerId() const { requirePlayed(); return scoreA_ > scoreB_ ? teamA_ : teamB_; }
    int loserId() const { requirePlayed(); return scoreA_ > scoreB_ ? teamB_ : teamA_; }

private:
    void requirePlayed() const {
        if (status_ != MatchStatus::Completed) throw InvalidOperationError("Match not played yet");
    }
    int id_, round_, teamA_, teamB_;
    int scoreA_ = 0, scoreB_ = 0;
    int slot_, ref_;
    MatchStatus status_ = MatchStatus::Scheduled;
};

} // namespace arena
