/*
 ArenaForge - C++ application layer
 Builds the JSON document the web front end renders.
 */
#pragma once
#include "arena_system.hpp"

#include <string>

namespace arena {

std::string buildSnapshot(const ArenaSystem& sys, SortAlgo leaderboardAlgo);

} // namespace arena
