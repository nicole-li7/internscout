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
    std::map<std::string, Listing> pinned;         // a private copy of every saved/applied listing, so they
                                                   // survive the posting closing or the cache being replaced
    std::vector<std::string> lastSearch;           // ids in the order last printed (1-based for the user)

    bool isNew(const std::string& id) const { return firstSeen.find(id) == firstSeen.end(); }
};
State loadState();
void saveState(const State& state);

// Keep saved/applied listings safe. Call this whenever `listings` changes:
//  * every saved/applied listing present in `listings` gets (re)pinned in `state`;
//  * if a pinned listing is missing from `listings` but the same job is there
//    under a different id (e.g. found on the company's own board), the
//    saved/applied mark moves to the new id;
//  * otherwise the pinned copy is appended to `listings`, flagged as closed,
//    so it still shows on the Saved / Applied pages.
// Returns true if `state` changed and should be saved.
bool reconcilePins(State& state, std::vector<Listing>& listings);
