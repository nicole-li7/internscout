// sources.hpp - where listings come from.
//
//  * Simplify   - a community-maintained list of tech internships on GitHub
//                 (thousands of listings, updated daily, internship-only).
//  * Greenhouse - the public job-board API used by many companies. We check the
//                 boards named in sources.json and keep only internship titles.
//  * Ashby      - same idea, different job-board provider.
//  * Lever      - same idea, different job-board provider.
//
// Add or remove companies by editing sources.json in the data folder
// (`internscout sources` prints the path).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "listing.hpp"

struct SourceConfig {
    bool simplify = true;
    std::vector<std::string> greenhouse;  // board tokens, e.g. "stripe"
    std::vector<std::string> ashby;       // e.g. "notion"
    std::vector<std::string> lever;       // e.g. "kraken"
};

// One line of feedback per source so the user knows what happened.
struct FetchReport {
    std::string name;
    int fetched = 0;      // listings kept (internships only)
    std::string error;    // empty on success
};

// Load sources.json (creating it with sensible defaults if it does not exist).
SourceConfig loadSourceConfig(const std::string& path);

// Fetch every configured source. `progress` is called with a short status string
// before each request so the terminal can show what is happening.
std::vector<Listing> fetchAllSources(const SourceConfig& config,
                                     std::vector<FetchReport>& reports,
                                     const std::function<void(const std::string&)>& progress);

// True if the title (or employment type) looks like an internship rather than a full-time role.
bool looksLikeInternship(const std::string& title);

// Pull "Summer 2027"-style terms out of free text. Empty if none are mentioned.
std::vector<std::string> extractTerms(const std::string& textToScan);
