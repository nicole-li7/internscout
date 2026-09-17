#include "eligibility.hpp"

#include <algorithm>
#include <regex>
#include <set>

#include "text.hpp"

namespace {

// "United States", "USA", "US", "U.S." -> "us"; "Canada", "Canadian" -> "canada"; etc.
std::string normaliseCountry(std::string c) {
    c = text::lower(c);
    if (c == "us" || c == "u.s." || c == "u.s" || c == "usa" || c == "united states" || c == "america" ||
        c == "american" || c == "united states of america")
        return "us";
    if (c == "canada" || c == "canadian") return "canada";
    if (c == "uk" || c == "u.k." || c == "united kingdom" || c == "british" || c == "britain") return "uk";
    return c;
}

std::string prettyCountry(const std::string& n) {
    if (n == "us") return "US";
    if (n == "canada") return "Canadian";
    if (n == "uk") return "UK";
    return n;
}

// Look for "must be a U.S. citizen" style requirements. Returns the normalised country or "".
std::string requiredCitizenship(const std::string& d) {
    static const std::regex patterns[] = {
        std::regex(R"((u\.?s\.?|united states|canadian|canada|u\.?k\.?|united kingdom) citizenship (is |will be )?(required|mandatory|necessary|a requirement))"),
        std::regex(R"(must (currently )?(be|hold|have) (a |an )?(u\.?s\.?|united states|canadian|canada|u\.?k\.?|united kingdom) citizen)"),
        std::regex(R"((u\.?s\.?|united states|canadian|u\.?k\.?) citizens? only)"),
        std::regex(R"(only (u\.?s\.?|united states|canadian|u\.?k\.?) citizens)"),
        std::regex(R"(requires? (u\.?s\.?|united states|canadian|u\.?k\.?) citizenship)"),
        std::regex(R"(open (only )?to (u\.?s\.?|united states|canadian|u\.?k\.?) citizens)"),
        std::regex(R"((u\.?s\.?|united states) person(s)? (only|required|status))"),
    };
    std::smatch m;
    for (const std::regex& re : patterns)
        if (std::regex_search(d, m, re)) return normaliseCountry(m[1].str());

    // A security clearance in practice means US citizenship.
    static const std::regex clearance(
        R"((requires?|must (be able to )?(obtain|hold|have|maintain)|ability to obtain|eligib\w+ (for|to obtain)) (a |an )?(active )?(u\.?s\.? |dod |government |top secret |secret |ts/sci )?(security )?clearance)");
    if (std::regex_search(d, clearance)) return "us";
    return "";
}

bool refusesSponsorship(const std::string& d) {
    static const std::regex patterns[] = {
        std::regex(R"((will not|won't|cannot|can't|unable to|not able to|does not|do not|doesn't|don't|is not able to|are not able to) (currently )?(be able to )?(provide|offer|consider|support|provide or support|sponsor)\w* ?(visa |work |employment |immigration )?(visa )?sponsor)"),
        std::regex(R"(no (visa |work |immigration )?sponsorship)"),
        std::regex(R"(sponsorship (is |will |would )?(not|isn't|unavailable))"),
        std::regex(R"(not (eligible|available|open) for (\w+ ){0,3}sponsorship)"),
        std::regex(R"(ineligible for (\w+ ){0,3}sponsorship)"),
        std::regex(R"(without (the need for |requiring |need of |any )?(current or future |now or in the future |visa |employer |employment |work )?sponsorship)"),
        std::regex(R"(authori[sz]ed to work (in|for) the (u\.?s\.?|united states|us|canada)[^.]{0,40}(without|on a permanent basis|permanently))"),
    };
    for (const std::regex& re : patterns)
        if (std::regex_search(d, re)) return true;
    return false;
}

// Graduation requirements. Looks at the sentence around each mention of
// graduating and pulls out the years in it, together with the words right
// next to each year ("by 2028", "2028 or later") that turn it into a bound.
std::string graduationProblem(const std::string& d, int gradYear) {
    if (gradYear <= 0) return "";
    static const std::regex mention(R"(\b(graduat(e|es|ed|ing|ion)|class of)\b)");
    static const std::regex yearRe(R"(\b(20[2-3]\d)\b)");
    static const std::regex seasonYear(R"((summer|fall|winter|spring|autumn) ?(20[2-3]\d))");

    for (auto it = std::sregex_iterator(d.begin(), d.end(), mention); it != std::sregex_iterator(); ++it) {
        size_t pos = static_cast<size_t>(it->position());
        // "undergraduate" / "postgraduate" are about the degree, not the date.
        if (pos >= 5 && (d.compare(pos - 5, 5, "under") == 0 || d.compare(pos - 4, 4, "post") == 0)) continue;

        // The sentence: from a little before the word to the next full stop / line break.
        size_t start = pos >= 30 ? pos - 30 : 0;
        size_t stop = d.find_first_of(".\n", pos);
        if (stop == std::string::npos || stop - start > 220) stop = std::min(d.size(), start + 220);
        std::string window = d.substr(start, stop - start);

        // "Summer 2027 internship" / "Spring 2028 start" name the term, not a graduation date;
        // "graduating spring 2028" does. Only ignore a season+year that is followed by a term-ish word.
        std::set<int> termYears;
        for (auto sy = std::sregex_iterator(window.begin(), window.end(), seasonYear); sy != std::sregex_iterator(); ++sy) {
            size_t endPos = static_cast<size_t>(sy->position() + sy->length());
            std::string after = window.substr(endPos, 20);
            if (after.find("intern") != std::string::npos || after.find("program") != std::string::npos ||
                after.find("co-op") != std::string::npos || after.find("start") != std::string::npos ||
                after.find("term") != std::string::npos || after.find("session") != std::string::npos)
                termYears.insert(std::stoi((*sy)[2].str()));
        }

        int lo = 0, hi = 0;                  // bounds implied by "by" / "or later"
        std::set<int> exact;                 // plain years ("in 2028", "2027 to 2028")
        for (auto y = std::sregex_iterator(window.begin(), window.end(), yearRe); y != std::sregex_iterator(); ++y) {
            int year = std::stoi((*y)[1].str());
            if (termYears.count(year)) continue;  // "Summer 2027 internship" is the term, not a graduation year
            size_t at = static_cast<size_t>(y->position());
            std::string before = window.substr(at >= 28 ? at - 28 : 0, at >= 28 ? 28 : at);
            std::string after = window.substr(at + 4, 16);
            bool upper = before.find(" by ") != std::string::npos || before.find("before") != std::string::npos ||
                         before.find("no later than") != std::string::npos || before.find("prior to") != std::string::npos ||
                         before.find("through") != std::string::npos || after.find("or earlier") != std::string::npos;
            bool lower = after.find("or later") != std::string::npos || after.find("or after") != std::string::npos ||
                         after.find("and beyond") != std::string::npos || after.find("onward") != std::string::npos ||
                         before.find("no earlier than") != std::string::npos || before.find("not graduat") != std::string::npos;
            if (upper && !lower) hi = hi ? std::min(hi, year) : year;
            else if (lower && !upper) lo = lo ? std::max(lo, year) : year;
            else exact.insert(year);
        }
        if (!exact.empty()) {
            int a = *exact.begin(), b = *exact.rbegin();
            if (gradYear < a || gradYear > b)
                return "wants graduation in " + (a == b ? std::to_string(a) : std::to_string(a) + "-" + std::to_string(b));
        }
        if (hi && gradYear > hi) return "wants graduation by " + std::to_string(hi);
        if (lo && gradYear < lo) return "wants graduation in " + std::to_string(lo) + " or later";
    }
    return "";
}

// "rising seniors" etc. Returns the minimum year of study the posting asks for, or 0.
int minimumYear(const std::string& d) {
    static const std::regex rising(R"(rising (sophomore|junior|senior)s?)");
    static const std::regex current(R"((current|currently a|currently enrolled as a) (sophomore|junior|senior))");
    int best = 0;  // the *lowest* year mentioned is the real minimum ("rising juniors or seniors" -> junior)
    auto consider = [&](const std::string& word, bool risingForm) {
        int y = word == "sophomore" ? 2 : word == "junior" ? 3 : 4;
        if (risingForm) y -= 1;  // a "rising junior" is currently in year 2
        if (best == 0 || y < best) best = y;
    };
    for (auto it = std::sregex_iterator(d.begin(), d.end(), rising); it != std::sregex_iterator(); ++it) consider((*it)[1], true);
    for (auto it = std::sregex_iterator(d.begin(), d.end(), current); it != std::sregex_iterator(); ++it) consider((*it)[2], false);
    return best;
}

}  // namespace

std::vector<std::string> disqualifiers(const Listing& l, const Profile& p) {
    std::vector<std::string> out;
    const std::string myCountry = normaliseCountry(p.country);

    // ---- structured fields (from the internship lists) ----
    if (text::contains(l.sponsorship, "citizenship")) {
        if (myCountry != "us") out.push_back("US citizenship required");
    } else if (p.needsSponsorship && text::contains(l.sponsorship, "does not offer")) {
        out.push_back("no visa sponsorship");
    }
    if (!l.degrees.empty()) {
        bool fits = false;
        for (const std::string& d : l.degrees)
            if (text::lower(d) == text::lower(p.level)) fits = true;
        if (!fits) out.push_back("for " + text::join(l.degrees, "/") + " students");
    }

    // ---- free text (company boards have descriptions; the lists do not) ----
    if (l.description.empty()) return out;
    const std::string d = text::lower(l.description.substr(0, 20000));

    std::string needCitizen = requiredCitizenship(d);
    bool alreadyCitizen = !out.empty() && out.front().find("citizenship") != std::string::npos;
    if (!needCitizen.empty() && needCitizen != myCountry && !alreadyCitizen)
        out.push_back(prettyCountry(needCitizen) + " citizenship required");

    if (p.needsSponsorship && refusesSponsorship(d) &&
        std::find(out.begin(), out.end(), "no visa sponsorship") == out.end())
        out.push_back("no visa sponsorship");

    std::string grad = graduationProblem(d, p.gradYear);
    if (!grad.empty()) out.push_back(grad);

    if (p.level == "Bachelor's" && p.yearOfStudy > 0) {
        int need = minimumYear(d);
        if (need > 0 && p.yearOfStudy < need) out.push_back("wants year " + std::to_string(need) + "+ students");
    }
    return out;
}
