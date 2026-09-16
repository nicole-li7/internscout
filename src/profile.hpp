// profile.hpp - who is looking for an internship?
// The profile is collected once by an interactive wizard (`internscout setup`)
// and saved as JSON so every later search can use it.
#pragma once

#include <string>
#include <vector>

struct Profile {
    std::string name;
    std::string school;
    std::string major;
    std::string minor;                    // optional
    std::string level = "Bachelor's";     // "Bachelor's", "Master's", "PhD"
    int yearOfStudy = 0;                  // 1 = first year ... (0 = not applicable / unknown)
    int gradYear = 0;                     // expected graduation year, e.g. 2028
    std::vector<std::string> keywords;    // interests: "machine learning", "backend", "fintech"...
    std::vector<std::string> locations;   // preferred: "Vancouver", "Toronto", "Seattle"...
    std::string country = "Canada";       // home country (used to prefer same-country roles)
    bool remoteOk = true;                 // happy with remote roles?
    bool needsSponsorship = false;        // needs a visa/work authorisation for the US?
    std::vector<std::string> terms;       // wanted terms: "Summer 2027", "Winter 2027" ...

    bool isComplete() const { return !major.empty() && !terms.empty(); }
};

// Ask the user questions in the terminal and build a profile.
// If `existing` is non-null its values are offered as defaults (press Enter to keep).
Profile runSetupWizard(const Profile* existing);

// Load / save the profile JSON file. loadProfile returns false if the file is missing or broken.
bool loadProfile(const std::string& path, Profile& out);
void saveProfile(const std::string& path, const Profile& profile);

// Pretty-print the profile.
void printProfile(const Profile& profile);

// Suggest internship terms based on today's date, e.g. in Sept 2026 -> {"Summer 2027"}.
std::vector<std::string> suggestTerms();
