// store.hpp - everything InternScout remembers between runs.
//
// All files live in one folder (see dataDir()):
//   profile.json      - your answers from `internscout setup`
//   sources.json      - which job boards to check
//   listings.json     - cached copy of the last fetch (so repeat searches are instant)
//   state.json        - which listings you have already seen / saved / applied to,
//                       plus the numbering of the last search so `open 3` works
#pragma once

#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "listing.hpp"

// ~/Library/Application Support/InternScout (created on first use)
std::string dataDir();
std::string profilePath();
std::string sourcesPath();

struct ListingCache {
    std::time_t fetchedAt = 0;
    std::vector<Listing> listings;
};
bool loadCache(ListingCache& out);
void saveCache(const ListingCache& cache);

struct State {
    std::map<std::string, std::time_t> firstSeen;  // listing id -> when we first saw it
    std::set<std::string> saved;                   // ids the user bookmarked
    std::set<std::string> applied;                 // ids the user has applied to
    std::vector<std::string> lastSearch;           // ids in the order last printed (1-based for the user)

    bool isNew(const std::string& id) const { return firstSeen.find(id) == firstSeen.end(); }
};
State loadState();
void saveState(const State& state);
