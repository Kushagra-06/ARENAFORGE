# ArenaForge: esports tournament, ranking and smart matchmaking system

Team GameDev, DSCPP-III-2026-T104. A C DSA engine, a C++ application layer, and a website that runs both.

## Layout

| Folder | Language | What it holds |
|---|---|---|
| `engine/` | C | `arena_engine.h` and the seven modules: sorting, heap, tree, hash table, queue, linked list, graph. Unchanged from the team's code. |
| `app/` | C++ | `models.hpp` (User, Player, Admin, Game, Team, Match, exceptions) plus the new application layer, below. |
| `tests/` | C++ | `tests.cpp`: 604 checks on the C engine and the C++ layer. |
| `web/` | HTML/JS | The site template, the build script, and the finished single-file site `arenaforge.html`. |

New C++ files: `engine_raii.hpp` (wrappers that own the C structures), `tournament.hpp/.cpp` (abstract `Tournament`; `KnockoutTournament`, `RoundRobinTournament`, `SwissTournament`; `Leaderboard`; Elo), `arena_system.hpp/.cpp` (controller, hash-table search, waitlist, file handling), `snapshot.cpp` and `json.hpp` (state for the web page), `web_api.cpp` (the C-callable functions the page uses).

## How the report maps to the code

| Report item | Where |
|---|---|
| Sorting for ranking and seeding | `ae_merge_sort`, `ae_quick_sort`; used by `Tournament::start`, `Leaderboard`, standings |
| Heap for matchmaking | `heap_*`, `ae_matchmake`; used by `SwissTournament::createRound` |
| Tree for brackets | `bracket_*`; used by `KnockoutTournament` |
| Hash table for search | `ht_*`; team names and gamer tags in `ArenaSystem` |
| Queue for registration and waitlist | `queue_*`; `Tournament::registerTeam` / `withdrawTeam` |
| Linked list for match history | `history_*`; also feeds the repeat-opponent penalty |
| Graph for scheduling conflicts | `graph_*`; `planSchedule` colours each batch of matches into time slots |
| Inheritance, abstraction, polymorphism | `User` to `Player`/`Admin`; abstract `Tournament` with three formats |
| Exception handling | `ArenaError` family; admin-only actions throw `PermissionError` |
| File handling | `ArenaSystem::save/load` write and read `Players.dat`, `Teams.dat`, `Tournaments.dat`, `Matches.dat`, `Results.dat`, `Rankings.dat`, `History.dat` |

## Build and run

```
make test    # native build with AddressSanitizer + UBSan, runs all 604 checks
make wasm    # compile the same C and C++ sources to WebAssembly and to plain JavaScript (needs Emscripten)
make site    # inline both into web/arenaforge.html
```

Open `web/arenaforge.html` in any browser. No server is needed. The page runs the WebAssembly build and falls back to the JavaScript build if WebAssembly is blocked.

## Rules the system enforces

- Teams must be full (the game's players per team) and match the tournament's game to register.
- A full tournament puts extra teams on a waitlist queue; a withdrawal promotes the first in line.
- Draws and negative scores are rejected. A match can't be played twice.
- Rosters lock while a team is in a live tournament.
- Creating tournaments, registering teams and entering results need admin mode.
- Ratings are Elo (K = 32). Fair Matchmaking Score = rating gap + 1000 per previous meeting.
