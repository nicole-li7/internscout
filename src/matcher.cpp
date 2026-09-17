#include "matcher.hpp"

#include <algorithm>
#include <set>

#include "eligibility.hpp"
#include "text.hpp"

// ---------- major -> keywords ----------

namespace {

struct MajorRule {
    std::vector<std::string> majorHints;   // if the major contains any of these...
    std::vector<std::string> keywords;     // ...these title words are a good sign
    std::vector<std::string> categories;   // ...and these Simplify categories fit
};

const std::vector<MajorRule>& majorRules() {
    static const std::vector<MajorRule> rules = {
        {{"computer science", "software", "computing", "informatics", "cs"},
         {"software", "developer", "engineer", "engineering", "programmer", "backend", "frontend",
          "front-end", "back-end", "full stack", "fullstack", "web", "mobile", "ios", "android",
          "cloud", "devops", "infrastructure", "platform", "systems", "security", "data",
          "machine learning", "ml", "ai", "sde", "swe", "technology", "technical", "qa", "test"},
         {"Software", "Software Engineering", "AI/ML/Data"}},
        {{"data science", "statistics", "stats", "analytics"},
         {"data", "analyst", "analytics", "machine learning", "ml", "ai", "statistic", "research",
          "quant", "science", "modeling", "insights", "bi"},
         {"AI/ML/Data", "Quant", "Data Science, AI & Machine Learning"}},
        {{"math", "mathematics", "applied math", "actuar"},
         {"quant", "quantitative", "research", "data", "analyst", "actuar", "risk", "trading",
          "modeling", "statistic"},
         {"Quant", "AI/ML/Data"}},
        {{"electrical", "computer engineering", "ece", "electronics", "hardware"},
         {"hardware", "electrical", "embedded", "firmware", "fpga", "asic", "circuit", "rf",
          "silicon", "chip", "verification", "validation", "systems", "robotics", "controls",
          "power", "signal", "test"},
         {"Hardware", "Hardware Engineering"}},
        {{"mechanical", "mechatronic", "aerospace", "robotics"},
         {"mechanical", "design", "manufacturing", "robotics", "hardware", "controls", "cad",
          "thermal", "mechatronic", "test", "systems", "product"},
         {"Hardware"}},
        {{"business", "commerce", "bcom", "management", "marketing", "finance", "accounting",
          "economics", "econ", "bba"},
         {"business", "finance", "financial", "accounting", "marketing", "analyst", "operations",
          "strategy", "consulting", "product", "sales", "growth", "investment", "banking",
          "audit", "economics", "supply chain", "hr", "people"},
         {"Product", "Quant"}},
        {{"design", "hci", "interaction", "media", "art", "communication"},
         {"design", "ux", "ui", "product design", "visual", "creative", "brand", "content",
          "communications", "marketing", "media", "graphic"},
         {"Product"}},
        {{"biology", "biochem", "chemistry", "life science", "biomed", "pharmac", "neuro",
          "health", "kinesiology", "microbio"},
         {"research", "lab", "laboratory", "biology", "chemistry", "clinical", "scientist",
          "science", "bioinformatics", "health", "pharma", "biotech", "r&d"},
         {"AI/ML/Data"}},
        {{"physics", "astronomy", "engineering physics"},
         {"research", "physics", "optics", "photonics", "quantum", "scientist", "simulation",
          "data", "hardware", "engineering"},
         {"Hardware", "AI/ML/Data"}},
        {{"civil", "environmental", "geolog", "chemical engineering", "materials"},
         {"civil", "structural", "environmental", "construction", "project", "field", "process",
          "materials", "sustainability", "energy", "engineering"},
         {"Hardware"}},
        {{"psychology", "sociology", "political", "international", "history", "english",
          "philosophy", "arts", "law", "public"},
         {"research", "policy", "communications", "writing", "content", "program", "people",
          "hr", "legal", "community", "operations", "editorial", "analyst"},
         {"Product"}},
    };
    return rules;
}

const MajorRule* findRule(const std::string& major) {
    std::string m = text::lower(major);
    for (const MajorRule& rule : majorRules())
        for (const std::string& hint : rule.majorHints)
            if (text::containsWord(m, hint) || m.find(hint) != std::string::npos) return &rule;
    return nullptr;
}

// Does the listing carry a term the user wants?  0 = no, 1 = unknown/NA, 2 = yes.
int termFit(const Listing& l, const Profile& p) {
    if (l.terms.empty()) return 1;
    bool anyReal = false;
    for (const std::string& t : l.terms) {
        if (t == "N/A" || t.empty()) continue;
        anyReal = true;
        for (const std::string& want : p.terms)
            if (text::lower(t) == text::lower(want)) return 2;
    }
    return anyReal ? 0 : 1;
}

std::string bestLocationMatch(const Listing& l, const Profile& p) {
    for (const std::string& pref : p.allPlaces())
        for (const std::string& loc : l.locations)
            if (text::contains(loc, pref)) return loc;
    return "";
}

bool inCountry(const Listing& l, const std::string& country) {
    std::string c = text::lower(country);
    // Simplify uses "Toronto, ON, Canada" and "Remote in Canada"; US listings usually omit the country.
    for (const std::string& loc : l.locations) {
        if (text::contains(loc, c)) return true;
        if ((c == "usa" || c == "united states" || c == "us") &&
            (text::contains(loc, "usa") || text::contains(loc, "united states")))
            return true;
    }
    return false;
}

}  // namespace

std::vector<std::string> keywordsForMajor(const std::string& major) {
    const MajorRule* rule = findRule(major);
    return rule ? rule->keywords : std::vector<std::string>{};
}

// ---------- scoring ----------

static Match scoreListing(const Listing& l, const Profile& p) {
    Match m;
    m.listing = &l;
    int score = 0;

    // 1. Term (up to 25). This is the single most important filter: a Fall 2026
    //    posting is useless to someone who wants Summer 2027.
    switch (termFit(l, p)) {
        case 2: score += 25; m.reasons.push_back(text::join(l.terms, "/")); break;
        case 1: score += 12; break;  // not stated; could still be right
        case 0: score += 0; m.termMismatch = true;
                m.warnings.push_back("term: " + text::join(l.terms, "/")); break;
    }

    // 2. Relevance to major + interests (up to 40).
    const std::string haystack = l.title + " " + l.category;
    const std::string deep = l.description.substr(0, 4000);
    int relevance = 0;
    std::vector<std::string> hits;

    // User keywords count double: they told us what they want.
    // (Interest labels like "Machine Learning / AI" expand to several words; see options.cpp.)
    for (const std::string& kw : p.allKeywords()) {
        if (text::containsWord(haystack, kw)) { relevance += 14; hits.push_back(kw); }
        else if (!deep.empty() && text::containsWord(deep, kw)) { relevance += 6; hits.push_back(kw); }
        if (hits.size() >= 4) break;  // enough evidence; keeps the "matches:" line short
    }
    const MajorRule* rule = findRule(p.major);
    const MajorRule* minorRule = p.minor.empty() ? nullptr : findRule(p.minor);
    for (const MajorRule* r : {rule, minorRule}) {
        if (!r) continue;
        int majorHits = 0;
        for (const std::string& kw : r->keywords)
            if (text::containsWord(haystack, kw) && ++majorHits <= 3) relevance += 6;
        for (const std::string& cat : r->categories)
            if (text::lower(l.category) == text::lower(cat)) relevance += 8;
        if (r == minorRule) relevance /= 2;  // minor counts less than major
    }
    // A listing with no keyword evidence at all is probably a different field.
    if (relevance == 0 && rule) m.warnings.push_back("no overlap with " + p.major);
    relevance = std::min(relevance, 40);
    score += relevance;
    if (!hits.empty()) m.reasons.push_back("matches: " + text::join(hits, ", "));
    else if (relevance > 0 && !l.category.empty()) m.reasons.push_back(l.category);

    // 3. Location (up to 20).
    std::string locHit = bestLocationMatch(l, p);
    if (!locHit.empty()) { score += 20; m.reasons.push_back(locHit); }
    else if (l.remote && p.remoteOk) { score += 14; m.reasons.push_back("remote"); }
    else if (inCountry(l, p.country)) { score += 10; m.reasons.push_back(p.country); }
    else if (p.locations.empty()) { score += 12; m.reasons.push_back("anywhere"); }

    // 4. Degree level (up to 10, or a penalty).
    if (l.degrees.empty()) {
        score += 6;
    } else {
        bool fits = false;
        for (const std::string& d : l.degrees)
            if (text::lower(d) == text::lower(p.level)) fits = true;
        if (fits) score += 10;
        else { score -= 15; m.warnings.push_back("wants " + text::join(l.degrees, "/")); }
    }

    // 5. Sponsorship.
    if (p.needsSponsorship) {
        if (text::contains(l.sponsorship, "does not offer") || text::contains(l.sponsorship, "citizenship")) {
            score -= 30;
            m.warnings.push_back("no sponsorship");
        } else if (text::contains(l.sponsorship, "offers")) {
            score += 5;
            m.reasons.push_back("sponsors visas");
        }
    }

    // 6. Freshness (up to 10). New postings are worth applying to quickly.
    int age = text::daysAgo(l.datePosted);
    if (age <= 7) { score += 10; m.reasons.push_back("posted this week"); }
    else if (age <= 30) score += 6;
    else if (age <= 90) score += 2;

    // 7. Hints in the description about year of study.
    if (!deep.empty() && p.yearOfStudy > 0) {
        std::string d = text::lower(deep);
        bool wantsSenior = d.find("rising senior") != std::string::npos || d.find("penultimate") != std::string::npos ||
                           d.find("final year") != std::string::npos;
        bool wantsJunior = d.find("rising junior") != std::string::npos;
        if (wantsSenior && p.yearOfStudy < 3) m.warnings.push_back("prefers senior students");
        if (wantsJunior && p.yearOfStudy < 2) m.warnings.push_back("prefers 2nd year+");
        if (p.gradYear && d.find("graduat") != std::string::npos && d.find(std::to_string(p.gradYear)) != std::string::npos) {
            score += 4;
            m.reasons.push_back("mentions class of " + std::to_string(p.gradYear));
        }
    }

    m.score = std::max(0, std::min(100, score));
    m.disqualifiers = disqualifiers(l, p);
    return m;
}

std::vector<Match> rankListings(const std::vector<Listing>& listings, const Profile& profile) {
    std::vector<Match> out;
    out.reserve(listings.size());
    for (const Listing& l : listings) out.push_back(scoreListing(l, profile));
    std::stable_sort(out.begin(), out.end(), [](const Match& a, const Match& b) {
        if (a.score != b.score) return a.score > b.score;
        return a.listing->datePosted > b.listing->datePosted;  // newer first on ties
    });
    return out;
}
