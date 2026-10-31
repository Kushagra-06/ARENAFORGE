/*
 ArenaForge - C++ application layer
 Tournament base class and the three formats.
 */
#include "tournament.hpp"

#include <algorithm>

namespace arena {

/* ======================= Tournament (base) ======================= */

Tournament::Tournament(int id, std::string name, const Game& game, int capacity, int stations,
                       SortAlgo algo, int createdBy)
    : id_(id), name_(std::move(name)), game_(game), capacity_(capacity), stations_(stations),
      algo_(algo), createdBy_(createdBy) {
    if (name_.empty()) throw InvalidOperationError("Tournament name cannot be empty");
    if (capacity_ < 2 || capacity_ > 64)
        throw InvalidOperationError("Capacity must be between 2 and 64 teams");
    if (stations_ < 1 || stations_ > 16)
        throw InvalidOperationError("Arenas (simultaneous matches) must be between 1 and 16");
}

int Tournament::championId(const TeamBook& teams) const {
    if (!isFinished()) return AE_TBD;
    std::vector<Standing> s = standings(teams);
    return s.empty() ? AE_TBD : s.front().teamId;
}

bool Tournament::isEntered(int teamId) const {
    return std::find(registered_.begin(), registered_.end(), teamId) != registered_.end()
        || waitlist_.contains(teamId);
}

const Match* Tournament::findMatch(int matchId) const {
    for (const Match& m : matches_) if (m.id() == matchId) return &m;
    return nullptr;
}

Match* Tournament::mutableMatch(int matchId) {
    for (Match& m : matches_) if (m.id() == matchId) return &m;
    return nullptr;
}

const Match* Tournament::findByRef(int ref) const {
    for (const Match& m : matches_) if (m.ref() == ref) return &m;
    return nullptr;
}

bool Tournament::hadBye(int teamId) const {
    for (const ByeRecord& b : byes_) if (b.teamId == teamId) return true;
    return false;
}

bool Tournament::roundComplete(int round) const {
    bool any = false;
    for (const Match& m : matches_) {
        if (m.round() != round) continue;
        any = true;
        if (m.status() != MatchStatus::Completed) return false;
    }
    return any;
}

RegResult Tournament::registerTeam(const Team& team) {
    if (status_ != TStatus::Registration)
        throw InvalidOperationError("Registration for " + name_ + " is closed");
    if (team.gameName() != game_.name())
        throw InvalidOperationError("Team " + team.name() + " plays " + team.gameName() + ", but " +
                                    name_ + " is a " + game_.name() + " tournament");
    if (!team.isFull())
        throw InvalidOperationError("Team " + team.name() + " has " + std::to_string(team.members().size()) +
                                    " of " + std::to_string(game_.playersPerTeam()) +
                                    " players and cannot enter yet");
    if (isEntered(team.id()))
        throw DuplicateError("Team " + team.name() + " is already entered in " + name_);

    if (static_cast<int>(registered_.size()) < capacity_) {
        registered_.push_back(team.id());
        return RegResult::Registered;
    }
    waitlist_.enqueue(team.id());                       /* tournament full: queue */
    return RegResult::Waitlisted;
}

int Tournament::withdrawTeam(int teamId) {
    if (status_ != TStatus::Registration)
        throw InvalidOperationError("Teams cannot withdraw after " + name_ + " has started");
    auto it = std::find(registered_.begin(), registered_.end(), teamId);
    if (it != registered_.end()) {
        registered_.erase(it);
        int next = 0;
        if (waitlist_.dequeue(next)) {                  /* first in line takes the free place */
            registered_.push_back(next);
            return next;
        }
        return -1;
    }
    if (waitlist_.remove(teamId)) return -1;            /* leave the middle of the queue */
    throw NotFoundError("That team is not entered in " + name_);
}

void Tournament::start(TeamBook& teams) {
    if (status_ != TStatus::Registration) throw InvalidOperationError(name_ + " has already started");
    if (registered_.size() < 2)
        throw InvalidOperationError("At least 2 teams must be registered to start " + name_);

    std::vector<RankEntry> ranked;                      /* seeding = C sort by rating */
    for (int id : registered_) ranked.push_back({id, teams.at(id).rating()});
    sortRanking(ranked, algo_);
    seeds_.clear();
    for (const RankEntry& r : ranked) seeds_.push_back({r.id, r.score});

    status_ = TStatus::InProgress;
    startImpl(teams);
    if (isFinished()) status_ = TStatus::Completed;
}

void Tournament::reportResult(int matchId, int scoreA, int scoreB, TeamBook& teams) {
    if (status_ != TStatus::InProgress) throw InvalidOperationError(name_ + " is not in progress");
    Match* m = mutableMatch(matchId);
    if (!m) throw NotFoundError("Match " + std::to_string(matchId) + " does not exist in " + name_);

    m->play(scoreA, scoreB);                            /* throws on draw / negative / replay */

    Team& winner = teams.at(m->winnerId());
    Team& loser  = teams.at(m->loserId());
    Elo::apply(winner, loser);
    winner.recordWin();
    loser.recordLoss();

    HistNode rec{};                                     /* linked-list history */
    rec.match_id = m->id();
    rec.round    = m->round();
    rec.team_a   = m->teamA();
    rec.team_b   = m->teamB();
    rec.score_a  = m->scoreA();
    rec.score_b  = m->scoreB();
    rec.winner   = m->winnerId();
    rec.next     = nullptr;
    history_.append(rec);
    order_.push_back(m->id());

    onMatchCompleted(*m, teams);
    if (isFinished()) status_ = TStatus::Completed;
}

std::vector<int> Tournament::addBatch(int round, const std::vector<std::pair<int, int>>& pairs,
                                      const std::vector<int>& refs) {
    SchedulePlan plan = planSchedule(pairs, stations_);  /* C graph colouring */
    std::vector<int> ids;
    for (std::size_t i = 0; i < pairs.size(); ++i) {
        int id = nextMatchId_++;
        matches_.emplace_back(id, round, pairs[i].first, pairs[i].second, plan.slots[i], refs[i]);
        ids.push_back(id);
    }
    return ids;
}

std::vector<Standing> Tournament::standings(const TeamBook& teams) const {
    std::map<int, Standing> table;
    for (int id : registered_) { Standing s; s.teamId = id; table[id] = s; }
    for (const Match& m : matches_) {
        if (m.status() != MatchStatus::Completed) continue;
        Standing& a = table[m.teamA()];
        Standing& b = table[m.teamB()];
        a.teamId = m.teamA(); b.teamId = m.teamB();
        a.played++; b.played++;
        a.diff += m.scoreA() - m.scoreB();
        b.diff += m.scoreB() - m.scoreA();
        if (m.scoreA() > m.scoreB()) { a.wins++; b.losses++; } else { b.wins++; a.losses++; }
    }
    std::vector<RankEntry> keys;
    for (auto& kv : table) {
        Standing& s = kv.second;
        s.wins += bonusWins(kv.first);
        auto it = teams.find(kv.first);
        s.key = standingKey(s, it != teams.end() ? it->second.rating() : 0.0);
        keys.push_back({kv.first, s.key});
    }
    sortRanking(keys, algo_);                           /* C sort again: table order */
    std::vector<Standing> out;
    for (const RankEntry& k : keys) out.push_back(table[k.id]);
    return out;
}

void Tournament::restoreMatch(int id, int round, int a, int b, int slot, int ref) {
    matches_.emplace_back(id, round, a, b, slot, ref);
    nextMatchId_ = std::max(nextMatchId_, id + 1);
}

void Tournament::restoreResult(int matchId, int scoreA, int scoreB) {
    Match* m = mutableMatch(matchId);
    if (!m) throw NotFoundError("Saved result refers to unknown match " + std::to_string(matchId));
    m->play(scoreA, scoreB);
}

void Tournament::restoreHistory(int matchId) {
    const Match* m = findMatch(matchId);
    if (!m || m->status() != MatchStatus::Completed)
        throw NotFoundError("Saved history refers to unplayed match " + std::to_string(matchId));
    HistNode rec{};
    rec.match_id = m->id();
    rec.round    = m->round();
    rec.team_a   = m->teamA();
    rec.team_b   = m->teamB();
    rec.score_a  = m->scoreA();
    rec.score_b  = m->scoreB();
    rec.winner   = m->winnerId();
    history_.append(rec);
    order_.push_back(matchId);
}

void Tournament::restoreStatus(TStatus s) {
    status_ = s;
    if (s != TStatus::Registration) {
        rebuild();
        if (status_ == TStatus::InProgress && isFinished()) status_ = TStatus::Completed;
    }
}

/* ======================= Knockout ======================= */

std::string KnockoutTournament::roundName(int round) const {
    int total = totalRounds();
    if (total == 0) return Tournament::roundName(round);
    if (round == total)     return "Final";
    if (round == total - 1) return "Semifinal";
    if (round == total - 2) return "Quarterfinal";
    return "Round of " + std::to_string(1 << (total - round + 1));
}

int KnockoutTournament::currentRound() const {
    if (!bracket_.root()) return 0;
    int r = 0;
    for (const Match& m : matches())
        if (m.status() == MatchStatus::Scheduled) r = (r == 0) ? m.round() : std::min(r, m.round());
    return r ? r : totalRounds();
}

void KnockoutTournament::startImpl(TeamBook&) {
    std::vector<int> ids;
    for (const Seed& s : seeds()) ids.push_back(s.teamId);
    if (!bracket_.build(ids)) throw InvalidOperationError("Could not build a bracket for this field");
    spawnReadyMatches();
}

/* Matches are scheduled round by round: once every earlier match is finished,
   the next batch of ready bracket nodes is graph-coloured into time slots. */
void KnockoutTournament::spawnReadyMatches() {
    std::vector<BracketNode*> fresh;
    for (BracketNode* n : bracket_.ready())
        if (!findByRef(n->node_id)) fresh.push_back(n);
    if (fresh.empty()) return;

    int r = fresh.front()->round;
    for (BracketNode* n : fresh) r = std::min(r, n->round);
    for (const Match& m : matches())
        if (m.status() == MatchStatus::Scheduled && m.round() < r) return;

    std::vector<std::pair<int, int>> pairs;
    std::vector<int> refs;
    for (BracketNode* n : fresh)
        if (n->round == r) {
            pairs.emplace_back(n->left->team_id, n->right->team_id);
            refs.push_back(n->node_id);
        }
    addBatch(r, pairs, refs);
}

void KnockoutTournament::onMatchCompleted(const Match& m, TeamBook&) {
    if (!bracket_.reportWinner(m.ref(), m.winnerId()))
        throw InvalidOperationError("The bracket rejected the result of match " + std::to_string(m.id()));
    spawnReadyMatches();
}

void KnockoutTournament::rebuild() {
    std::vector<int> ids;
    for (const Seed& s : seeds()) ids.push_back(s.teamId);
    if (ids.size() < 2 || !bracket_.build(ids)) return;
    for (const Match& m : matches())                    /* replay winners in creation order */
        if (m.status() == MatchStatus::Completed) bracket_.reportWinner(m.ref(), m.winnerId());
}

/* ======================= Round robin ======================= */

void RoundRobinTournament::startImpl(TeamBook&) {
    const std::vector<Seed>& s = seeds();
    std::vector<std::pair<int, int>> pairs;
    for (std::size_t i = 0; i < s.size(); ++i)
        for (std::size_t j = i + 1; j < s.size(); ++j) pairs.emplace_back(s[i].teamId, s[j].teamId);
    addBatch(1, pairs, std::vector<int>(pairs.size(), 0));
}

/* ======================= Swiss ======================= */

int SwissTournament::roundsFor(int teams) {
    int r = 0;
    while ((1 << r) < teams) ++r;
    return std::max(1, std::min(r, teams - 1));
}

void SwissTournament::startImpl(TeamBook& teams) {
    total_ = roundsFor(static_cast<int>(registered().size()));
    round_ = 0;
    createRound(teams);
}

/* Each round: rank teams by live rating, give a bye to the lowest team that has
   not had one, then let the C heap pick the cheapest Fair Matchmaking Score
   pairs (rating gap + penalty for repeat opponents). */
void SwissTournament::createRound(TeamBook& teams) {
    ++round_;
    std::vector<RankEntry> live;
    for (int id : registered()) live.push_back({id, teams.at(id).rating()});
    sortRanking(live, sortAlgo());

    if (live.size() % 2 == 1) {
        int pick = static_cast<int>(live.size()) - 1;
        for (int i = pick; i >= 0; --i)
            if (!hadBye(live[static_cast<std::size_t>(i)].id)) { pick = i; break; }
        addBye(round_, live[static_cast<std::size_t>(pick)].id);
        live.erase(live.begin() + pick);
    }

    std::vector<MatchPair> out(live.size() / 2);
    int k = ae_matchmake(live.data(), static_cast<int>(live.size()), history().raw(),
                         out.data(), static_cast<int>(out.size()));
    std::vector<std::pair<int, int>> pairs;
    for (int i = 0; i < k; ++i) pairs.emplace_back(out[static_cast<std::size_t>(i)].team_a,
                                                   out[static_cast<std::size_t>(i)].team_b);
    addBatch(round_, pairs, std::vector<int>(pairs.size(), 0));
}

void SwissTournament::onMatchCompleted(const Match&, TeamBook& teams) {
    if (roundComplete(round_) && round_ < total_) createRound(teams);
}

void SwissTournament::rebuild() {
    total_ = roundsFor(static_cast<int>(registered().size()));
    round_ = 0;
    for (const Match& m : matches()) round_ = std::max(round_, m.round());
}

int SwissTournament::bonusWins(int teamId) const {
    int n = 0;
    for (const ByeRecord& b : byes()) if (b.teamId == teamId) ++n;
    return n;
}

/* ======================= factory ======================= */

std::unique_ptr<Tournament> makeTournament(Format f, int id, std::string name, const Game& game,
                                           int capacity, int stations, SortAlgo algo, int createdBy) {
    switch (f) {
        case Format::Knockout:
            return std::make_unique<KnockoutTournament>(id, std::move(name), game, capacity, stations, algo, createdBy);
        case Format::RoundRobin:
            if (capacity > 16) throw InvalidOperationError("Round robin supports at most 16 teams");
            return std::make_unique<RoundRobinTournament>(id, std::move(name), game, capacity, stations, algo, createdBy);
        case Format::Swiss:
            return std::make_unique<SwissTournament>(id, std::move(name), game, capacity, stations, algo, createdBy);
    }
    throw InvalidOperationError("Unknown tournament format");
}

} // namespace arena
