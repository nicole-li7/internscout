#include "store.hpp"

#include <sys/stat.h>

#include <cstdlib>
#include <fstream>
#include <iostream>

#include "json.hpp"

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
    json j{
        {"firstSeen", firstSeen},
        {"saved", std::vector<std::string>(s.saved.begin(), s.saved.end())},
        {"applied", std::vector<std::string>(s.applied.begin(), s.applied.end())},
        {"lastSearch", s.lastSearch},
    };
    std::ofstream out(statePath());
    out << j.dump();
}
