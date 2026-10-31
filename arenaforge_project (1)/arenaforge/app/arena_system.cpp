/*
 ArenaForge - C++ application layer
 ArenaSystem implementation (registration, teams, tournaments, search, files).
 */
#include "arena_system.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <fstream>
#include <sstream>

namespace arena {

namespace {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    std::size_t a = 0, b = s.size();
    while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) ++a;
    while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) --b;
    return s.substr(a, b - a);
}

/* ----- file helpers: fields are '|' separated; '|', '%' and line breaks are escaped ----- */
std::string enc(const std::string& s) {
    std::string o;
    for (char c : s) {
        if (c == '%') o += "%25";
        else if (c == '|') o += "%7C";
        else if (c == '\n') o += "%0A";
        else if (c == '\r') o += "%0D";
        else o += c;
    }
    return o;
}

std::string dec(const std::string& s) {
    std::string o;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '%' && i + 2 < s.size()) {
            std::string h = s.substr(i + 1, 2);
            if (h == "25") { o += '%'; i += 2; continue; }
            if (h == "7C") { o += '|'; i += 2; continue; }
            if (h == "0A") { o += '\n'; i += 2; continue; }
            if (h == "0D") { o += '\r'; i += 2; continue; }
        }
        o += s[i];
    }
    return o;
}

std::vector<std::string> split(const std::string& s, char sep) {
    std::vector<std::string> out;
    std::string cur;
    for (char c : s) {
        if (c == sep) { out.push_back(cur); cur.clear(); }
        else cur += c;
    }
    out.push_back(cur);
    return out;
}

std::vector<std::string> readLines(const std::string& path) {
    std::ifstream f(path);
    if (!f) throw NotFoundError("Saved file not found: " + path);
    std::vector<std::string> out;
    std::string line;
    while (std::getline(f, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line.empty() || line[0] == '#') continue;
        out.push_back(line);
    }
    return out;
}

int toInt(const std::string& s) {
    try { return std::stoi(s); }
    catch (const std::exception&) { throw ArenaError("Corrupt data file: bad number '" + s + "'"); }
}

double toDouble(const std::string& s) {
    try { return std::stod(s); }
    catch (const std::exception&) { throw ArenaError("Corrupt data file: bad number '" + s + "'"); }
}

void need(const std::vector<std::string>& f, std::size_t n, const char* file) {
    if (f.size() < n) throw ArenaError(std::string("Corrupt data file: short record in ") + file);
}

std::ofstream openOut(const std::string& path, const char* header) {
    std::ofstream f(path);
    if (!f) throw ArenaError("Cannot write " + path);
    f << header << '\n';
    return f;
}

} // namespace

/* ============================== workspace ============================== */

void ArenaSystem::reset() {
    games_.clear();
    games_.emplace_back("Valorant", 5);
    games_.emplace_back("Rocket League", 3);
    games_.emplace_back("Fortnite Duos", 2);
    games_.emplace_back("Chess Blitz", 1);
    users_.clear();
    teams_.clear();
    tournaments_.clear();
    teamIndex_.clear();
    tagIndex_.clear();
    emailIndex_.clear();
    nextUserId_ = nextTeamId_ = nextTournamentId_ = 1;

    const std::string email = "admin@arenaforge.gg";
    users_.emplace(nextUserId_, std::make_unique<Admin>(nextUserId_, "Tournament Admin", email, 1));
    emailIndex_.put(email, nextUserId_);
    ++nextUserId_;
}

void ArenaSystem::requireAdmin(int actor, const std::string& action) const {
    auto it = users_.find(actor);
    const Admin* a = (it == users_.end()) ? nullptr : dynamic_cast<const Admin*>(it->second.get());
    if (!a || !a->canManageTournaments())
        throw PermissionError("Only an administrator can " + action + ".");
}

const Game& ArenaSystem::gameByName(const std::string& name) const {
    for (const Game& g : games_) if (lower(g.name()) == lower(name)) return g;
    throw NotFoundError("Unknown game: " + name);
}

Team& ArenaSystem::teamRef(int id) {
    auto it = teams_.find(id);
    if (it == teams_.end()) throw NotFoundError("Team #" + std::to_string(id) + " does not exist");
    return it->second;
}

Player& ArenaSystem::playerRef(int id) {
    auto it = users_.find(id);
    Player* p = (it == users_.end()) ? nullptr : dynamic_cast<Player*>(it->second.get());
    if (!p) throw NotFoundError("Player #" + std::to_string(id) + " does not exist");
    return *p;
}

Tournament& ArenaSystem::tourRef(int id) {
    for (auto& t : tournaments_) if (t->id() == id) return *t;
    throw NotFoundError("Tournament #" + std::to_string(id) + " does not exist");
}

const Tournament& ArenaSystem::tournament(int id) const {
    for (const auto& t : tournaments_) if (t->id() == id) return *t;
    throw NotFoundError("Tournament #" + std::to_string(id) + " does not exist");
}

bool ArenaSystem::teamLocked(int teamId) const {
    for (const auto& t : tournaments_)
        if (t->status() == TStatus::InProgress && t->isEntered(teamId)) return true;
    return false;
}

/* ============================== games / users / teams ============================== */

void ArenaSystem::addGame(const std::string& rawName, int playersPerTeam) {
    std::string name = trim(rawName);
    Game g(name, playersPerTeam);                       /* validates */
    for (const Game& x : games_)
        if (lower(x.name()) == lower(name)) throw DuplicateError("Game " + name + " already exists");
    games_.push_back(g);
}

int ArenaSystem::registerPlayer(const std::string& rawName, const std::string& rawEmail,
                                const std::string& rawTag) {
    std::string name = trim(rawName), email = trim(rawEmail), tag = trim(rawTag);
    int id = nextUserId_;
    auto p = std::make_unique<Player>(id, name, email, tag);   /* validates name + email */
    if (tag.empty()) throw ArenaError("Gamer tag cannot be empty");
    if (tagIndex_.find(tag))     throw DuplicateError("Gamer tag " + tag + " is already taken");
    if (emailIndex_.find(email)) throw DuplicateError("Email " + email + " is already registered");
    tagIndex_.put(tag, id);
    emailIndex_.put(email, id);
    users_.emplace(id, std::move(p));
    ++nextUserId_;
    return id;
}

int ArenaSystem::createTeam(const std::string& rawName, const std::string& gameName, double rating) {
    std::string name = trim(rawName);
    if (rating < 100.0 || rating > 4000.0) throw ArenaError("Starting rating must be between 100 and 4000");
    int id = nextTeamId_;
    Team team(id, name, gameByName(gameName));                 /* validates name */
    if (teamIndex_.find(name)) throw DuplicateError("A team called " + name + " already exists");
    team.setRating(rating);
    teamIndex_.put(name, id);
    teams_.emplace(id, std::move(team));
    ++nextTeamId_;
    return id;
}

void ArenaSystem::joinTeam(int teamId, int playerId) {
    Team& t = teamRef(teamId);
    Player& p = playerRef(playerId);
    if (teamLocked(teamId))
        throw InvalidOperationError("Team " + t.name() + " is in a live tournament, so its roster is locked");
    t.addPlayer(p);
}

void ArenaSystem::leaveTeam(int teamId, int playerId) {
    Team& t = teamRef(teamId);
    Player& p = playerRef(playerId);
    if (teamLocked(teamId))
        throw InvalidOperationError("Team " + t.name() + " is in a live tournament, so its roster is locked");
    t.removePlayer(p);
}

/* ============================== tournaments ============================== */

int ArenaSystem::createTournament(int actor, const std::string& rawName, Format format,
                                  const std::string& gameName, int capacity, int stations, SortAlgo algo) {
    requireAdmin(actor, "create tournaments");
    std::string name = trim(rawName);
    for (const auto& t : tournaments_)
        if (lower(t->name()) == lower(name)) throw DuplicateError("A tournament called " + name + " already exists");
    int id = nextTournamentId_;
    tournaments_.push_back(makeTournament(format, id, name, gameByName(gameName), capacity, stations, algo, actor));
    ++nextTournamentId_;
    return id;
}

RegResult ArenaSystem::registerTeamForTournament(int actor, int tid, int teamId) {
    requireAdmin(actor, "manage participants");
    return tourRef(tid).registerTeam(teamRef(teamId));
}

std::pair<int, int> ArenaSystem::registerAllFullTeams(int actor, int tid) {
    requireAdmin(actor, "manage participants");
    Tournament& t = tourRef(tid);
    int reg = 0, wait = 0;
    for (auto& kv : teams_) {
        Team& tm = kv.second;
        if (!tm.isFull() || tm.gameName() != t.game().name() || t.isEntered(tm.id())) continue;
        if (t.registerTeam(tm) == RegResult::Registered) ++reg; else ++wait;
    }
    return {reg, wait};
}

int ArenaSystem::withdrawTeam(int actor, int tid, int teamId) {
    requireAdmin(actor, "manage participants");
    return tourRef(tid).withdrawTeam(teamId);
}

void ArenaSystem::startTournament(int actor, int tid) {
    requireAdmin(actor, "start tournaments");
    tourRef(tid).start(teams_);
}

void ArenaSystem::reportResult(int actor, int tid, int matchId, int scoreA, int scoreB) {
    requireAdmin(actor, "enter match results");
    tourRef(tid).reportResult(matchId, scoreA, scoreB, teams_);
}

std::vector<RankEntry> ArenaSystem::leaderboard(SortAlgo algo) const {
    Leaderboard lb;
    lb.rebuild(teams_, algo);
    return lb.entries();
}

/* ============================== sample data ============================== */

void ArenaSystem::loadSample(int teamCount) {
    if (teamCount < 2 || teamCount > 16)
        throw InvalidOperationError("Sample data supports 2 to 16 teams");
    reset();
    static const char* teamNames[16] = {
        "Neon Vipers", "Iron Foxes", "Crimson Tide", "Static Wolves", "Pixel Pirates", "Night Owls",
        "Solar Flare", "Glitch Gang", "Void Runners", "Frost Giants", "Turbo Llamas", "Shadow Syndicate",
        "Quantum Quokkas", "Rogue Waves", "Copper Kings", "Last Stand"};
    static const int ratings[16] = {1180, 1420, 960, 1310, 1075, 1250, 880, 1385,
                                    1010, 1190, 745, 1120, 1290, 835, 1040, 920};
    static const char* tags[16] = {"Blaze", "Nova", "Orbit", "Cipher", "Vortex", "Pixel", "Rune", "Echo",
                                   "Zephyr", "Atlas", "Ember", "Quill", "Fable", "Onyx", "Raven", "Sprout"};
    static const char* firsts[24] = {"Aarav", "Meera", "Kabir", "Isha", "Rohan", "Anaya", "Vihaan", "Diya",
                                     "Arjun", "Tara", "Neel", "Sana", "Dev", "Riya", "Yash", "Zoya",
                                     "Ishaan", "Kiara", "Reyansh", "Mira", "Aditya", "Naina", "Veer", "Alia"};
    static const char* lasts[12] = {"Sharma", "Rawat", "Bhatt", "Kandwal", "Negi", "Joshi",
                                    "Rana", "Bisht", "Thapa", "Pant", "Rautela", "Chauhan"};

    const std::string game = "Rocket League";
    const int perTeam = gameByName(game).playersPerTeam();
    for (int t = 0; t < teamCount; ++t) {
        int tid = createTeam(teamNames[t], game, ratings[t]);
        for (int m = 0; m < perTeam; ++m) {
            int idx = t * perTeam + m;
            std::string tag = std::string(tags[t]) + "_" + std::to_string(m + 1);
            std::string name = std::string(firsts[idx % 24]) + " " + lasts[(idx * 5) % 12];
            int pid = registerPlayer(name, lower(tag) + "@arena.gg", tag);
            joinTeam(tid, pid);
        }
    }
}

/* ============================== file handling ============================== */

void ArenaSystem::save(const std::string& dir) const {
    ::mkdir(dir.c_str(), 0777);                          /* ok if it already exists */

    {   /* Players.dat */
        std::ofstream f = openOut(dir + "/Players.dat", "#ArenaForge Players v1");
        for (const auto& kv : users_) {
            const User& u = *kv.second;
            if (const Player* p = dynamic_cast<const Player*>(&u))
                f << "P|" << u.id() << '|' << enc(u.name()) << '|' << enc(u.email()) << '|' << enc(p->gamerTag()) << '\n';
            else
                f << "A|" << u.id() << '|' << enc(u.name()) << '|' << enc(u.email()) << '\n';
        }
    }
    {   /* Teams.dat (games first) */
        std::ofstream f = openOut(dir + "/Teams.dat", "#ArenaForge Teams v1");
        for (const Game& g : games_) f << "G|" << enc(g.name()) << '|' << g.playersPerTeam() << '\n';
        for (const auto& kv : teams_) {
            const Team& t = kv.second;
            f << "T|" << t.id() << '|' << enc(t.name()) << '|' << enc(t.gameName()) << '|';
            for (std::size_t i = 0; i < t.members().size(); ++i) f << (i ? "," : "") << t.members()[i];
            f << '\n';
        }
    }
    {   /* Rankings.dat */
        std::ofstream f = openOut(dir + "/Rankings.dat", "#ArenaForge Rankings v1");
        char buf[64];
        for (const auto& kv : teams_) {
            const Team& t = kv.second;
            std::snprintf(buf, sizeof buf, "%.6f", t.rating());
            f << "R|" << t.id() << '|' << buf << '|' << t.wins() << '|' << t.losses() << '\n';
        }
    }
    std::ofstream tf = openOut(dir + "/Tournaments.dat", "#ArenaForge Tournaments v1");
    std::ofstream mf = openOut(dir + "/Matches.dat", "#ArenaForge Matches v1");
    std::ofstream rf = openOut(dir + "/Results.dat", "#ArenaForge Results v1");
    std::ofstream hf = openOut(dir + "/History.dat", "#ArenaForge History v1");
    char buf[64];
    for (const auto& tp : tournaments_) {
        const Tournament& t = *tp;
        tf << "T|" << t.id() << '|' << enc(t.name()) << '|' << static_cast<int>(t.format()) << '|'
           << enc(t.game().name()) << '|' << t.capacity() << '|' << t.stations() << '|'
           << static_cast<int>(t.sortAlgo()) << '|' << static_cast<int>(t.status()) << '|' << t.createdBy() << '\n';
        for (int id : t.registered()) tf << "R|" << t.id() << '|' << id << '\n';
        for (int id : t.waitlist())   tf << "W|" << t.id() << '|' << id << '\n';
        for (const Seed& s : t.seeds()) {
            std::snprintf(buf, sizeof buf, "%.6f", s.rating);
            tf << "S|" << t.id() << '|' << s.teamId << '|' << buf << '\n';
        }
        for (const ByeRecord& b : t.byes()) tf << "B|" << t.id() << '|' << b.round << '|' << b.teamId << '\n';
        for (const Match& m : t.matches()) {
            mf << "M|" << t.id() << '|' << m.id() << '|' << m.round() << '|' << m.teamA() << '|'
               << m.teamB() << '|' << m.slot() << '|' << m.ref() << '\n';
            if (m.status() == MatchStatus::Completed)
                rf << "X|" << t.id() << '|' << m.id() << '|' << m.scoreA() << '|' << m.scoreB() << '\n';
        }
        for (int mid : t.completionOrder()) hf << "H|" << t.id() << '|' << mid << '\n';
    }
    if (!tf || !mf || !rf || !hf) throw ArenaError("Could not finish writing the data files");
}

void ArenaSystem::load(const std::string& dir) {
    try {
        std::vector<std::string> players = readLines(dir + "/Players.dat");
        std::vector<std::string> teams   = readLines(dir + "/Teams.dat");
        std::vector<std::string> ranks   = readLines(dir + "/Rankings.dat");
        std::vector<std::string> tours   = readLines(dir + "/Tournaments.dat");
        std::vector<std::string> matches = readLines(dir + "/Matches.dat");
        std::vector<std::string> results = readLines(dir + "/Results.dat");
        std::vector<std::string> history = readLines(dir + "/History.dat");

        reset();
        users_.clear();
        emailIndex_.clear();
        games_.clear();
        int maxUser = 0, maxTeam = 0, maxTour = 0;

        for (const std::string& line : players) {            /* Players.dat */
            std::vector<std::string> f = split(line, '|');
            need(f, 4, "Players.dat");
            int id = toInt(f[1]);
            std::string name = dec(f[2]), email = dec(f[3]);
            if (f[0] == "P") {
                need(f, 5, "Players.dat");
                std::string tag = dec(f[4]);
                users_.emplace(id, std::make_unique<Player>(id, name, email, tag));
                tagIndex_.put(tag, id);
            } else {
                users_.emplace(id, std::make_unique<Admin>(id, name, email, 1));
            }
            emailIndex_.put(email, id);
            maxUser = std::max(maxUser, id);
        }
        for (const std::string& line : teams) {              /* Teams.dat */
            std::vector<std::string> f = split(line, '|');
            if (f[0] == "G") {
                need(f, 3, "Teams.dat");
                games_.emplace_back(dec(f[1]), toInt(f[2]));
            } else {
                need(f, 5, "Teams.dat");
                int id = toInt(f[1]);
                std::string name = dec(f[2]);
                teams_.emplace(id, Team(id, name, gameByName(dec(f[3]))));
                teamIndex_.put(name, id);
                if (!f[4].empty())
                    for (const std::string& m : split(f[4], ',')) teams_.at(id).addPlayer(playerRef(toInt(m)));
                maxTeam = std::max(maxTeam, id);
            }
        }
        for (const std::string& line : ranks) {              /* Rankings.dat */
            std::vector<std::string> f = split(line, '|');
            need(f, 5, "Rankings.dat");
            Team& t = teamRef(toInt(f[1]));
            t.setRating(toDouble(f[2]));
            for (int i = 0, n = toInt(f[3]); i < n; ++i) t.recordWin();
            for (int i = 0, n = toInt(f[4]); i < n; ++i) t.recordLoss();
        }

        std::map<int, TStatus> pending;
        for (const std::string& line : tours) {              /* Tournaments.dat */
            std::vector<std::string> f = split(line, '|');
            need(f, 3, "Tournaments.dat");
            if (f[0] == "T") {
                need(f, 10, "Tournaments.dat");
                int id = toInt(f[1]);
                tournaments_.push_back(makeTournament(static_cast<Format>(toInt(f[3])), id, dec(f[2]),
                                                      gameByName(dec(f[4])), toInt(f[5]), toInt(f[6]),
                                                      static_cast<SortAlgo>(toInt(f[7])), toInt(f[9])));
                pending[id] = static_cast<TStatus>(toInt(f[8]));
                maxTour = std::max(maxTour, id);
            } else if (f[0] == "R") {
                tourRef(toInt(f[1])).restoreRegistered(toInt(f[2]));
            } else if (f[0] == "W") {
                tourRef(toInt(f[1])).restoreWaitlisted(toInt(f[2]));
            } else if (f[0] == "S") {
                need(f, 4, "Tournaments.dat");
                tourRef(toInt(f[1])).restoreSeed(toInt(f[2]), toDouble(f[3]));
            } else if (f[0] == "B") {
                need(f, 4, "Tournaments.dat");
                tourRef(toInt(f[1])).restoreBye(toInt(f[2]), toInt(f[3]));
            }
        }
        for (const std::string& line : matches) {            /* Matches.dat */
            std::vector<std::string> f = split(line, '|');
            need(f, 8, "Matches.dat");
            tourRef(toInt(f[1])).restoreMatch(toInt(f[2]), toInt(f[3]), toInt(f[4]), toInt(f[5]),
                                              toInt(f[6]), toInt(f[7]));
        }
        for (const std::string& line : results) {            /* Results.dat */
            std::vector<std::string> f = split(line, '|');
            need(f, 5, "Results.dat");
            tourRef(toInt(f[1])).restoreResult(toInt(f[2]), toInt(f[3]), toInt(f[4]));
        }
        for (const std::string& line : history) {            /* History.dat */
            std::vector<std::string> f = split(line, '|');
            need(f, 3, "History.dat");
            tourRef(toInt(f[1])).restoreHistory(toInt(f[2]));
        }
        for (const auto& kv : pending) tourRef(kv.first).restoreStatus(kv.second);

        nextUserId_ = maxUser + 1;
        nextTeamId_ = maxTeam + 1;
        nextTournamentId_ = maxTour + 1;
    } catch (...) {
        reset();
        throw;
    }
}

} // namespace arena
