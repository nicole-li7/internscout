// eligibility.hpp - hard requirements that rule a listing out completely.
//
// Scoring (matcher.cpp) decides how *good* a fit is. This decides whether you
// are allowed to apply at all. A listing is ineligible when it states a
// requirement the profile cannot meet:
//
//   * citizenship of a country that is not yours ("must be a U.S. citizen",
//     "requires a security clearance")
//   * no visa sponsorship, when you said you need it ("will not sponsor",
//     "authorized to work without sponsorship")
//   * a graduation window you fall outside of ("graduating by June 2028",
//     "expected graduation between Dec 2027 and May 2029")
//   * a degree level you do not have ("Master's or PhD students only")
//   * a year of study you have not reached ("rising seniors")
//
// Each rule returns a short human-readable reason so the app can show why.
#pragma once

#include <string>
#include <vector>

#include "listing.hpp"
#include "profile.hpp"

// Empty means eligible. Otherwise one reason per failed requirement.
std::vector<std::string> disqualifiers(const Listing& listing, const Profile& profile);
