#include "store.hpp"

#include <sys/stat.h>

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>

#include "json.hpp"
#include "sources.hpp"

using nlohmann::json;

std::string dataDir() {
    static const std::string dir = [] {
        const char* home = std::getenv("HOME");
        std::string base = home ? home : ".";
#ifdef __APPLE__
        std::string d = base + "/Library/Application Support/InternScout";
#else
        std::string d = base + "/.internscout";
#endif
        mkdir(d.c_str(), 0755);  // harmless if it already exists
        return d;
    }();
    return dir;
}

std::string profilePath() { return dataDir() + "/profile.json"; }
std::string sourcesPath() { return dataDir() + "/sources.json"; }
static std::string cachePath() { return dataDir() + "/listings.json"; }
static std::string statePath() { return dataDir() + "/state.json"; }

bool loadCache(ListingCache& out) {
    std::ifstream in(cachePath());
    if (!in) return false;
    try {
        json j;
        in >> j;
        out.fetchedAt = static_cast<std::time_t>(j.value("fetchedAt", 0LL));
        out.listings = j.value("listings", std::vector<Listing>{});
        return !out.listings.empty();
    } catch (...) {
        return false;
    }
}

void saveCache(const ListingCache& cache) {
    json j{{"fetchedAt", static_cast<long long>(cache.fetchedAt)}, {"listings", cache.listings}};
    std::ofstream out(cachePath());
    out << j.dump();
}

State loadState() {
    State s;
    std::ifstream in(statePath());
    if (!in) return s;
    try {
        json j;
        in >> j;
        if (j.contains("firstSeen") && j["firstSeen"].is_object())
            for (const auto& [id, t] : j["firstSeen"].items())
                if (t.is_number()) s.firstSeen[id] = static_cast<std::time_t>(t.get<long long>());
        if (j.contains("saved") && j["saved"].is_array())
            for (const auto& id : j["saved"]) if (id.is_string()) s.saved.insert(id.get<std::string>());
        if (j.contains("applied") && j["applied"].is_array())
            for (const auto& id : j["applied"]) if (id.is_string()) s.applied.insert(id.get<std::string>());
        if (j.contains("pinned") && j["pinned"].is_object())
            for (const auto& [id, l] : j["pinned"].items())
                if (l.is_object()) s.pinned[id] = l.get<Listing>();
        if (j.contains("lastSearch") && j["lastSearch"].is_array())
            for (const auto& id : j["lastSearch"]) if (id.is_string()) s.lastSearch.push_back(id.get<std::string>());
    } catch (const std::exception& e) {
        // A corrupt state file just means we forget what was seen; not fatal.
        std::cerr << "warning: could not read " << statePath() << ": " << e.what() << "\n";
    }
    return s;
}

void saveState(const State& s) {
    json firstSeen = json::object();
    for (const auto& [id, t] : s.firstSeen) firstSeen[id] = static_cast<long long>(t);
    json pinned = json::object();
    for (const auto& [id, l] : s.pinned) pinned[id] = l;
    json j{
        {"firstSeen", firstSeen},
        {"saved", std::vector<std::string>(s.saved.begin(), s.saved.end())},
        {"applied", std::vector<std::string>(s.applied.begin(), s.applied.end())},
        {"pinned", pinned},
        {"lastSearch", s.lastSearch},
    };
    // Write to a temporary file first, then rename, so a crash mid-write can
    // never leave a half-written state.json behind.
    std::string tmp = statePath() + ".tmp";
    {
        std::ofstream out(tmp);
        out << j.dump();
    }
    std::rename(tmp.c_str(), statePath().c_str());
}

bool reconcilePins(State& state, std::vector<Listing>& listings) {
    bool changed = false;
    std::map<std::string, size_t> byId;
    std::map<std::string, size_t> byKey;
    for (size_t i = 0; i < listings.size(); ++i) {
        byId[listings[i].id] = i;
        byKey.emplace(dedupeKey(listings[i].url), i);
    }

    std::set<std::string> marked = state.saved;
    marked.insert(state.applied.begin(), state.applied.end());

    for (const std::string& id : marked) {
        auto hit = byId.find(id);
        if (hit != byId.end()) {
            // Still live: refresh our private copy.
            Listing copy = listings[hit->second];
            copy.closed = false;
            state.pinned[id] = copy;
            changed = true;
            continue;
        }
        auto pin = state.pinned.find(id);
        if (pin == state.pinned.end()) continue;  // marked before pinning existed and already gone; nothing to restore

        auto dup = byKey.find(dedupeKey(pin->second.url));
        if (dup != byKey.end()) {
            // Same job, new id (for example now fetched from the company board). Move the mark.
            const std::string newId = listings[dup->second].id;
            if (state.saved.erase(id)) state.saved.insert(newId);
            if (state.applied.erase(id)) state.applied.insert(newId);
            state.pinned[newId] = listings[dup->second];
            state.pinned.erase(id);
            changed = true;
        } else {
            // Gone from every source: show our copy, flagged as closed.
            Listing copy = pin->second;
            copy.closed = true;
            listings.push_back(copy);
            byId[copy.id] = listings.size() - 1;
        }
    }

    // Drop pins for things that are no longer saved or applied.
    for (auto it = state.pinned.begin(); it != state.pinned.end();) {
        if (!state.saved.count(it->first) && !state.applied.count(it->first)) { it = state.pinned.erase(it); changed = true; }
        else ++it;
    }
    return changed;
}
