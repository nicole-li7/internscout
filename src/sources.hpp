// sources.hpp - where listings come from.
//
// Two layers:
//
//  1. Feeds: community-maintained internship lists on GitHub (Simplify and a
//     smaller second list). Thousands of curated postings, updated daily.
//
//  2. Company job boards, queried directly through the public APIs of the
//     job-board software companies use (Greenhouse, Ashby, Lever, Workday,
//     SmartRecruiters, Workable). Every internship on those boards is picked
//     up, including ones nobody submitted to the feeds and non-tech roles.
//
//     With "discover" on (the default), the boards to check are found
//     automatically from the links in the feeds - several hundred companies,
//     many of them small. You can also list boards by hand in sources.json in
//     the data folder (`internscout sources` prints the path).
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "listing.hpp"

struct SourceConfig {
    std::vector<std::string> feeds;           // URLs of Simplify-format JSON lists
    bool discover = true;                     // find company boards from feed links
    std::vector<std::string> greenhouse;      // board tokens, e.g. "stripe"
    std::vector<std::string> ashby;           // e.g. "notion"
    std::vector<std::string> lever;           // e.g. "kraken"
    std::vector<std::string> smartrecruiters; // e.g. "BoschGroup"
    std::vector<std::string> workable;        // e.g. "altom-transport"
    std::vector<std::string> workday;         // "tenant.wd5.myworkdayjobs.com/SiteName"
};

// One line of feedback per source so the user knows what happened.
struct FetchReport {
    std::string name;
    int fetched = 0;      // listings kept (internships only)
    std::string error;    // empty on success
};

// Load sources.json (creating it with sensible defaults if it does not exist) / save it back.
SourceConfig loadSourceConfig(const std::string& path);
void saveSourceConfig(const std::string& path, const SourceConfig& config);

// Fetch every configured source. `progress` is called with a short status string
// as work proceeds so the UI can show what is happening.
std::vector<Listing> fetchAllSources(const SourceConfig& config,
                                     std::vector<FetchReport>& reports,
                                     const std::function<void(const std::string&)>& progress);

// True if the title (or employment type) looks like an internship rather than a full-time role.
bool looksLikeInternship(const std::string& title);

// True if the title says co-op / coop (as opposed to a plain internship).
bool looksLikeCoop(const std::string& title);

// Reduce a posting link to a stable key so the same job reached by two different
// links (feed vs. company board) is recognised as one.
std::string dedupeKey(const std::string& url);

// Pull "Summer 2027"-style terms out of free text. Empty if none are mentioned.
std::vector<std::string> extractTerms(const std::string& textToScan);
