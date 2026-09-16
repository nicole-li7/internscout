#include "profile.hpp"

#include <ctime>
#include <fstream>
#include <iostream>

#include "json.hpp"
#include "options.hpp"
#include "text.hpp"
#include "ui.hpp"

using nlohmann::json;

// ---------- JSON conversion ----------

static void to_json(json& j, const Profile& p) {
    j = json{
        {"name", p.name},           {"school", p.school},       {"major", p.major},
        {"minor", p.minor},         {"level", p.level},         {"yearOfStudy", p.yearOfStudy},
        {"gradYear", p.gradYear},   {"interests", p.interests}, {"keywords", p.keywords},   {"locations", p.locations},
        {"country", p.country},     {"remoteOk", p.remoteOk},   {"needsSponsorship", p.needsSponsorship},
        {"terms", p.terms},
    };
}

static void from_json(const json& j, Profile& p) {
    p.name = j.value("name", "");
    p.school = j.value("school", "");
    p.major = j.value("major", "");
    p.minor = j.value("minor", "");
    p.level = j.value("level", "Bachelor's");
    p.yearOfStudy = j.value("yearOfStudy", 0);
    p.gradYear = j.value("gradYear", 0);
    p.interests = j.value("interests", std::vector<std::string>{});
    p.keywords = j.value("keywords", std::vector<std::string>{});
    p.locations = j.value("locations", std::vector<std::string>{});
    p.country = j.value("country", "Canada");
    p.remoteOk = j.value("remoteOk", true);
    p.needsSponsorship = j.value("needsSponsorship", false);
    p.terms = j.value("terms", std::vector<std::string>{});
}

std::vector<std::string> Profile::allKeywords() const {
    std::vector<std::string> out;
    for (const std::string& label : interests)
        for (const std::string& kw : options::keywordsForInterest(label)) out.push_back(kw);
    for (const std::string& kw : keywords) out.push_back(kw);
    return out;
}

std::vector<std::string> Profile::allPlaces() const {
    std::vector<std::string> out;
    for (const std::string& label : locations)
        for (const std::string& place : options::placesForLocation(label)) out.push_back(place);
    return out;
}

bool loadProfile(const std::string& path, Profile& out) {
    std::ifstream in(path);
    if (!in) return false;
    try {
        json j;
        in >> j;
        out = j.get<Profile>();
        return true;
    } catch (const std::exception& e) {
        std::cerr << ui::warn("Could not read profile at " + path + ": " + e.what()) << "\n";
        return false;
    }
}

void saveProfile(const std::string& path, const Profile& profile) {
    std::ofstream out(path);
    out << json(profile).dump(2) << "\n";
}

// ---------- term suggestion ----------

std::vector<std::string> suggestTerms() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_r(&now, &tm);
    int year = tm.tm_year + 1900;
    int month = tm.tm_mon + 1;  // 1..12

    // Summer internships are mostly posted Aug-Feb for the *following* summer.
    // If it's already March or later, "this" summer is largely filled, so look one further out.
    int summerYear = (month >= 3) ? year + 1 : year;
    return {"Summer " + std::to_string(summerYear)};
}

// ---------- the wizard ----------

namespace {

// Ask a question; return what the user typed, or `def` if they just pressed Enter.
std::string ask(const std::string& question, const std::string& def = "") {
    std::cout << ui::bold(question);
    if (!def.empty()) std::cout << ui::dim(" [" + def + "]");
    std::cout << ": " << std::flush;
    std::string line;
    if (!std::getline(std::cin, line)) return def;
    line = text::trim(line);
    return line.empty() ? def : line;
}

bool askYesNo(const std::string& question, bool def) {
    while (true) {
        std::string a = text::lower(ask(question + " (y/n)", def ? "y" : "n"));
        if (a == "y" || a == "yes") return true;
        if (a == "n" || a == "no") return false;
        std::cout << ui::warn("Please answer y or n.") << "\n";
    }
}

int askInt(const std::string& question, int def, int lo, int hi) {
    while (true) {
        std::string a = ask(question, def ? std::to_string(def) : "");
        if (a.empty()) return 0;
        try {
            int v = std::stoi(a);
            if (v >= lo && v <= hi) return v;
        } catch (...) {}
        std::cout << ui::warn("Please enter a number between " + std::to_string(lo) + " and " +
                              std::to_string(hi) + ".")
                  << "\n";
    }
}

std::vector<std::string> askList(const std::string& question, const std::vector<std::string>& def) {
    std::string a = ask(question, text::join(def, ", "));
    return text::split(a, ',');
}

}  // namespace

Profile runSetupWizard(const Profile* existing) {
    Profile p = existing ? *existing : Profile{};
    if (p.terms.empty()) p.terms = suggestTerms();

    std::cout << "\n" << ui::header("InternScout setup") << "\n";
    std::cout << ui::dim("Answer a few questions so searches can be ranked for you.\n"
                         "Press Enter to accept the value shown in [brackets].\n")
              << "\n";

    p.name = ask("Your name", p.name);
    p.school = ask("School", p.school);
    p.major = ask("Major (e.g. Computer Science, Statistics, Commerce)", p.major);
    p.minor = ask("Minor (optional)", p.minor);

    std::cout << "\n" << ui::dim("Degree level: 1 = Bachelor's, 2 = Master's, 3 = PhD") << "\n";
    int levelDefault = p.level == "Master's" ? 2 : p.level == "PhD" ? 3 : 1;
    int level = askInt("Degree level", levelDefault, 1, 3);
    p.level = level == 2 ? "Master's" : level == 3 ? "PhD" : "Bachelor's";

    p.yearOfStudy = askInt("Current year of study (1-6)", p.yearOfStudy, 1, 6);
    p.gradYear = askInt("Expected graduation year", p.gradYear, 2024, 2040);

    std::cout << "\n" << ui::dim("Interests are matched against job titles and descriptions.\n"
                                 "Examples: software, machine learning, backend, data analysis,\n"
                                 "product, finance, hardware, UX design, research")
              << "\n";
    p.keywords = askList("Interests / keywords (comma separated)", p.keywords);

    std::cout << "\n" << ui::dim("Locations are matched against listing locations.\n"
                                 "Examples: Vancouver, Toronto, Seattle, San Francisco, Canada")
              << "\n";
    p.locations = askList("Preferred locations (comma separated)", p.locations);
    p.country = ask("Your home country", p.country);
    p.remoteOk = askYesNo("Are remote internships OK?", p.remoteOk);
    p.needsSponsorship = askYesNo("Would you need visa sponsorship to work in the US?", p.needsSponsorship);

    std::cout << "\n" << ui::dim("Which internship terms are you looking for?\n"
                                 "Examples: Summer 2027, Winter 2027, Fall 2027")
              << "\n";
    p.terms = askList("Terms (comma separated)", p.terms);
    if (p.terms.empty()) p.terms = suggestTerms();

    std::cout << "\n" << ui::ok("Profile saved.") << "\n";
    return p;
}

void printProfile(const Profile& p) {
    auto row = [](const std::string& k, const std::string& v) {
        std::cout << "  " << ui::dim(text::pad(k, 18)) << (v.empty() ? ui::dim("(none)") : v) << "\n";
    };
    std::cout << ui::header("Your profile") << "\n";
    row("Name", p.name);
    row("School", p.school);
    row("Major", p.major + (p.minor.empty() ? "" : " (minor: " + p.minor + ")"));
    row("Level", p.level + (p.yearOfStudy ? ", year " + std::to_string(p.yearOfStudy) : ""));
    row("Graduating", p.gradYear ? std::to_string(p.gradYear) : "");
    row("Interests", p.interests.empty() && p.keywords.empty() ? "any" : text::join(p.interests, ", ") +
                     (p.keywords.empty() ? "" : (p.interests.empty() ? "" : ", ") + text::join(p.keywords, ", ")));
    row("Locations", p.locations.empty() ? "anywhere" : text::join(p.locations, ", "));
    row("Country", p.country);
    row("Remote OK", p.remoteOk ? "yes" : "no");
    row("Needs US visa", p.needsSponsorship ? "yes" : "no");
    row("Terms", text::join(p.terms, ", "));
}
