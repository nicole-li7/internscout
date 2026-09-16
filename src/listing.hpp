// listing.hpp - the one data structure every part of the app agrees on.
// Each job board returns its own shape; the fetchers in sources.cpp convert
// them all into this.
#pragma once

#include <ctime>
#include <string>
#include <vector>

#include "json.hpp"

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
};

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
}
