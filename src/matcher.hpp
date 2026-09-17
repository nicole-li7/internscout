// matcher.hpp - decides how well each listing fits the profile.
//
// Every listing gets a score from 0 to 100 plus a list of short reasons
// ("Summer 2027", "Vancouver", "matches: machine learning") so the user can
// see *why* something ranked where it did.
#pragma once

#include <string>
#include <vector>

#include "listing.hpp"
#include "profile.hpp"

struct Match {
    const Listing* listing = nullptr;
    int score = 0;
    std::vector<std::string> reasons;   // positives
    std::vector<std::string> warnings;  // negatives (wrong term, needs PhD, no sponsorship...)
    bool termMismatch = false;          // listing is explicitly for a term the user did not ask for
    std::vector<std::string> disqualifiers;  // hard requirements the profile fails (see eligibility.hpp)
    bool ineligible() const { return !disqualifiers.empty(); }
};

// Score every listing and return them sorted best-first.
std::vector<Match> rankListings(const std::vector<Listing>& listings, const Profile& profile);

// Built-in knowledge: which words in a job title suggest it suits a given major.
// e.g. "Computer Science" -> {"software", "developer", "engineer", ...}
std::vector<std::string> keywordsForMajor(const std::string& major);
