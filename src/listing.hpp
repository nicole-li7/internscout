// listing.hpp - the one data structure every part of the app agrees on.
// Each job board returns its own shape; the fetchers in sources.cpp convert
// them all into this.
#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "json.hpp"

// What the description says about who may apply, found once when the listing is
// fetched (see scanEligibility in eligibility.hpp) and saved with it, so ranking
// only has to compare these against the profile instead of re-reading the text.
struct GradWindow {                      // one sentence about graduating
    int first = 0, last = 0;             // plain years mentioned ("in 2027 or 2028"), 0 = none
    int notAfter = 0;                    // "by 2028", 0 = none
    int notBefore = 0;                   // "2028 or later", 0 = none
};
struct EligibilityScan {
    int version = 0;                     // 0 = not scanned yet; see kEligibilityScanVersion
    std::string citizenship;             // "us", "canada", "uk", ... if citizenship is required, else ""
    bool refusesSponsorship = false;     // "will not sponsor", "without sponsorship", ...
    std::vector<GradWindow> graduation;  // in the order they appear
    int minimumYear = 0;                 // "rising seniors" -> 3, 0 = not stated
};

struct Listing {
    std::string id;                      // unique across sources, e.g. "simplify:4bab85d2-..."
    std::string source;                  // "Simplify", "Greenhouse", "Lever", "Ashby"
    std::string company;
    std::string title;
    std::vector<std::string> locations;  // "Vancouver, BC, Canada", "Remote in USA", ...
    std::vector<std::string> terms;      // "Summer 2027", "Fall 2026", ... ("N/A" if unknown)
    std::vector<std::string> degrees;    // "Bachelor's", "Master's", "PhD" (empty = not stated)
    std::string category;                // "Software", "AI/ML/Data", "Hardware", ... (may be empty)
    std::string sponsorship;             // "Offers Sponsorship", "Does Not Offer Sponsorship", ...
    std::string url;                     // where to apply
    std::string description;             // plain text, often empty (Simplify doesn't provide one)
    std::time_t datePosted = 0;          // Unix timestamp
    bool remote = false;                 // true if any location says remote
    bool closed = false;                 // true if this is a saved/applied copy of a posting that has since disappeared
    EligibilityScan eligibility;         // filled in by scanEligibility()
};

inline void to_json(nlohmann::json& j, const GradWindow& g) { j = nlohmann::json{g.first, g.last, g.notAfter, g.notBefore}; }
inline void from_json(const nlohmann::json& j, GradWindow& g) {
    if (!j.is_array() || j.size() != 4) return;
    g.first = j[0].get<int>(); g.last = j[1].get<int>(); g.notAfter = j[2].get<int>(); g.notBefore = j[3].get<int>();
}

// These two functions let nlohmann::json convert Listing <-> JSON automatically,
// which is how we cache listings on disk.
inline void to_json(nlohmann::json& j, const Listing& l) {
    j = nlohmann::json{
        {"id", l.id},           {"source", l.source},       {"company", l.company},
        {"title", l.title},     {"locations", l.locations}, {"terms", l.terms},
        {"degrees", l.degrees}, {"category", l.category},   {"sponsorship", l.sponsorship},
        {"url", l.url},         {"description", l.description},
        {"datePosted", static_cast<long long>(l.datePosted)}, {"remote", l.remote}, {"closed", l.closed},
    };
    const EligibilityScan& e = l.eligibility;
    if (e.version)
        j["eligibility"] = nlohmann::json{{"version", e.version}, {"citizenship", e.citizenship},
                                          {"refusesSponsorship", e.refusesSponsorship}, {"graduation", e.graduation},
                                          {"minimumYear", e.minimumYear}};
}

inline void from_json(const nlohmann::json& j, Listing& l) {
    l.id = j.value("id", "");
    l.source = j.value("source", "");
    l.company = j.value("company", "");
    l.title = j.value("title", "");
    l.locations = j.value("locations", std::vector<std::string>{});
    l.terms = j.value("terms", std::vector<std::string>{});
    l.degrees = j.value("degrees", std::vector<std::string>{});
    l.category = j.value("category", "");
    l.sponsorship = j.value("sponsorship", "");
    l.url = j.value("url", "");
    l.description = j.value("description", "");
    l.datePosted = static_cast<std::time_t>(j.value("datePosted", 0LL));
    l.remote = j.value("remote", false);
    l.closed = j.value("closed", false);
    auto e = j.find("eligibility");  // missing in caches written by older versions; they get rescanned
    if (e != j.end() && e->is_object()) {
        l.eligibility.version = e->value("version", 0);
        l.eligibility.citizenship = e->value("citizenship", "");
        l.eligibility.refusesSponsorship = e->value("refusesSponsorship", false);
        l.eligibility.graduation = e->value("graduation", std::vector<GradWindow>{});
        l.eligibility.minimumYear = e->value("minimumYear", 0);
    }
}
