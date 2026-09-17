// main.cpp - the command line interface.
//
//   internscout setup            answer the profile questions
//   internscout search           fetch + rank internships for you
//   internscout watch            keep checking and notify about new matches
//   internscout show N           details for result N
//   internscout open N           open result N in your browser
//   internscout save N           bookmark result N
//   internscout applied N        mark result N as applied
//   internscout saved            list bookmarks and applications
//   internscout profile          print your profile
//   internscout sources          show where listings come from
//
// Run `internscout help` for the option flags.

#include <chrono>
#include <cstdlib>
#include <ctime>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include "matcher.hpp"
#include "profile.hpp"
#include "sources.hpp"
#include "store.hpp"
#include "text.hpp"
#include "ui.hpp"

namespace {

// ---------- option parsing ----------

struct Options {
    std::string command = "help";
    std::vector<std::string> positional;  // e.g. the N in "open 3"
    bool refresh = false;                 // ignore the cache and fetch again
    bool all = false;                     // do not hide weak / wrong-term matches
    int limit = 25;
    int minScore = 40;
    int everyMinutes = 30;
    std::string type;                     // "", "intern" or "coop"
};

int toInt(const std::string& s, int fallback) {
    try { return std::stoi(s); } catch (...) { return fallback; }
}

Options parseArgs(int argc, char** argv) {
    Options o;
    if (argc >= 2) o.command = argv[1];
    for (int i = 2; i < argc; ++i) {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return (i + 1 < argc) ? argv[++i] : ""; };
        if (a == "--refresh" || a == "-r") o.refresh = true;
        else if (a == "--all" || a == "-a") o.all = true;
        else if (a == "--limit" || a == "-n") o.limit = toInt(next(), o.limit);
        else if (a == "--min" || a == "-m") o.minScore = toInt(next(), o.minScore);
        else if (a == "--every" || a == "-e") o.everyMinutes = toInt(next(), o.everyMinutes);
        else if (a == "--type" || a == "-t") o.type = text::lower(next());
        else if (a.rfind("--", 0) == 0) std::cerr << ui::warn("unknown option " + a) << "\n";
        else o.positional.push_back(a);
    }
    return o;
}

void printHelp() {
    std::cout << ui::bold("InternScout") << " - finds internships that fit you\n\n"
              << ui::header("Commands") << "\n"
              << "  setup             answer a few questions about you (year, major, interests...)\n"
              << "  search            fetch listings and show the best matches\n"
              << "  watch             keep searching in the background; notify on new matches\n"
              << "  show N            full details for result N from the last search\n"
              << "  open N            open result N in your browser\n"
              << "  save N            bookmark result N          (unsave N to remove)\n"
              << "  applied N         mark result N as applied\n"
              << "  saved             list bookmarks and applications\n"
              << "  profile           print your profile\n"
              << "  sources           list job boards being checked; `sources deep on|off`\n\n"
              << ui::header("Options") << "\n"
              << "  --refresh, -r     re-download listings even if the cache is fresh\n"
              << "  --all, -a         include weak matches, other terms and listings you are not eligible for\n"
              << "  --limit N, -n N   how many results to show          (default 25)\n"
              << "  --min N, -m N     minimum score 0-100 to show       (default 40)\n"
              << "  --every N, -e N   minutes between checks in watch    (default 30)\n"
              << "  --type intern|coop  only internships, or only co-ops\n\n"
              << ui::dim("Data folder: " + dataDir()) << "\n";
}

// ---------- shared steps ----------

Profile requireProfile() {
    Profile p;
    if (!loadProfile(profilePath(), p) || !p.isComplete()) {
        std::cout << ui::warn("No profile yet - let's set one up first.") << "\n";
        p = runSetupWizard(nullptr);
        saveProfile(profilePath(), p);
    }
    return p;
}

// Get listings: from the cache if it is fresh enough, otherwise from the internet.
// Saved/applied listings are always kept (see reconcilePins in store.hpp).
ListingCache getListings(bool forceRefresh, bool quiet = false) {
    const std::time_t maxAge = 6 * 60 * 60;  // 6 hours
    ListingCache cache;
    bool haveCache = loadCache(cache);
    {
        // Pin saved/applied listings from the cache we already have *before* replacing it.
        State st = loadState();
        if (reconcilePins(st, cache.listings)) saveState(st);
    }
    if (haveCache && !forceRefresh && std::time(nullptr) - cache.fetchedAt < maxAge) {
        if (!quiet) {
            long minutes = (std::time(nullptr) - cache.fetchedAt) / 60;
            std::cout << ui::dim("Using listings fetched " + std::to_string(minutes) +
                                 " min ago (use --refresh to re-download).") << "\n";
        }
        return cache;
    }

    SourceConfig config = loadSourceConfig(sourcesPath());
    std::vector<FetchReport> reports;
    std::vector<Listing> fresh = fetchAllSources(config, reports, [&](const std::string& what) {
        if (!quiet) ui::status(what);
    });
    ui::clearStatus();

    int okSources = 0, total = 0;
    std::vector<std::string> failures;
    for (const FetchReport& r : reports) {
        if (r.error.empty()) { ++okSources; total += r.fetched; }
        else failures.push_back(r.name + " (" + r.error + ")");
    }
    if (!quiet) {
        std::cout << ui::ok(std::to_string(total) + " internship listings from " + std::to_string(okSources) +
                            " sources") << "\n";
        if (!failures.empty()) {
            std::vector<std::string> shown(failures.begin(), failures.begin() + std::min<size_t>(5, failures.size()));
            std::string more = failures.size() > 5 ? ", and " + std::to_string(failures.size() - 5) + " more" : "";
            std::cout << ui::dim("  " + std::to_string(failures.size()) + " boards skipped (no public API or removed): " +
                                 text::join(shown, ", ") + more) << "\n";
        }
    }

    if (fresh.empty()) {
        if (haveCache) {
            std::cout << ui::warn("Could not reach any source; using the older cached listings.") << "\n";
            return cache;
        }
        std::cout << ui::fail("Could not fetch any listings. Are you online?") << "\n";
        std::exit(1);
    }
    cache.fetchedAt = std::time(nullptr);
    cache.listings = std::move(fresh);
    saveCache(cache);
    {
        State st = loadState();
        if (reconcilePins(st, cache.listings)) saveState(st);
    }
    return cache;
}

// For commands that only read the cache (show / open / saved ...).
ListingCache cachedListings() {
    ListingCache cache;
    loadCache(cache);
    State st = loadState();
    if (reconcilePins(st, cache.listings)) saveState(st);
    return cache;
}

// Remember that we have seen every listing in this batch (so "NEW" means new since last time).
void markAllSeen(State& state, const std::vector<Listing>& listings) {
    std::time_t now = std::time(nullptr);
    for (const Listing& l : listings)
        if (state.isNew(l.id)) state.firstSeen[l.id] = now;
}

// ---------- printing ----------

std::string tag(const State& state, const Listing& l) {
    const int w = 9;  // column width
    if (state.applied.count(l.id)) return ui::magenta(text::pad("APPLIED", w));
    if (state.saved.count(l.id)) return ui::cyan(text::pad("SAVED", w));
    if (state.isNew(l.id)) return ui::green(text::pad("NEW", w));
    return std::string(w, ' ');
}

void printMatchRow(int index, const Match& m, const State& state) {
    const Listing& l = *m.listing;
    int width = ui::terminalWidth();
    // Fixed columns: index(4) score(5) tag(8) posted(11) -> everything else shares the rest.
    int flexible = std::max(40, width - 4 - 5 - 9 - 11 - 4);
    int companyW = std::min(22, flexible / 4);
    int locationW = std::min(24, flexible / 4);
    int titleW = flexible - companyW - locationW;

    std::string location = l.locations.empty() ? "-" : l.locations.front();
    if (l.locations.size() > 1) location += " +" + std::to_string(l.locations.size() - 1);
    if (l.closed) location = "(closed) " + location;

    std::cout << text::pad(std::to_string(index) + ".", 4)
              << ui::scoreColour(m.score, text::pad(std::to_string(m.score), 5))
              << tag(state, l)
              << ui::bold(text::pad(text::truncate(l.company, companyW - 1), companyW))
              << text::pad(text::truncate(l.title, titleW - 1), titleW)
              << ui::dim(text::pad(text::truncate(location, locationW - 1), locationW))
              << ui::dim(text::formatDate(l.datePosted)) << "\n";

    // Second line: why it scored what it did.
    std::string why = text::join(m.reasons, " | ");
    if (!m.warnings.empty()) why += (why.empty() ? "" : "  ") + ui::yellow("! " + text::join(m.warnings, ", "));
    if (m.ineligible()) why += (why.empty() ? "" : "  ") + ui::red("NOT ELIGIBLE: " + text::join(m.disqualifiers, ", "));
    if (!why.empty()) std::cout << "         " << ui::dim(why) << "\n";
}

void printListingDetail(const Match& m, const State& state) {
    const Listing& l = *m.listing;
    std::cout << "\n" << ui::header(l.title) << "\n";
    auto row = [](const std::string& k, const std::string& v) {
        if (!v.empty()) std::cout << "  " << ui::dim(text::pad(k, 12)) << v << "\n";
    };
    row("Company", l.company);
    row("Score", ui::scoreColour(m.score, std::to_string(m.score) + "/100"));
    row("Why", text::join(m.reasons, ", "));
    if (!m.warnings.empty()) row("Watch out", ui::yellow(text::join(m.warnings, ", ")));
    if (m.ineligible()) row("Not eligible", ui::red(text::join(m.disqualifiers, ", ")));
    row("Location", text::join(l.locations, "; "));
    row("Term", text::join(l.terms, ", "));
    row("Degrees", text::join(l.degrees, ", "));
    row("Category", l.category);
    row("Sponsorship", l.sponsorship);
    row("Posted", text::formatDate(l.datePosted));
    row("Source", l.source);
    row("Status", std::string(state.applied.count(l.id) ? "applied" : state.saved.count(l.id) ? "saved" : "") +
                  (l.closed ? " (posting no longer listed)" : ""));
    row("Apply", ui::cyan(l.url));
    if (!l.description.empty()) {
        std::cout << "\n" << ui::dim(text::truncate(l.description, 2000)) << "\n";
    }
    std::cout << "\n";
}

// Find result N (1-based) from the last search.
const Listing* resolveResult(const std::string& arg, const State& state, const std::vector<Listing>& listings) {
    int n = toInt(arg, 0);
    if (n < 1 || n > static_cast<int>(state.lastSearch.size())) {
        std::cout << ui::fail("No result #" + arg + ". Run `internscout search` first, then use a number from the list.")
                  << "\n";
        return nullptr;
    }
    const std::string& id = state.lastSearch[n - 1];
    for (const Listing& l : listings)
        if (l.id == id) return &l;
    std::cout << ui::fail("That listing is no longer in the cache. Run `internscout search --refresh`.") << "\n";
    return nullptr;
}

Match rescore(const Listing& l, const Profile& p) {
    std::vector<Listing> one{l};
    Match m = rankListings(one, p).front();
    m.listing = &l;  // point at the caller's copy, not our temporary
    return m;
}

// ---------- commands ----------

int cmdSetup() {
    Profile existing;
    bool have = loadProfile(profilePath(), existing);
    Profile p = runSetupWizard(have ? &existing : nullptr);
    saveProfile(profilePath(), p);
    std::cout << "\n";
    printProfile(p);
    std::cout << "\n" << ui::dim("Next: run `internscout search`") << "\n";
    return 0;
}

int cmdSearch(const Options& o) {
    Profile profile = requireProfile();
    ListingCache cache = getListings(o.refresh);
    State state = loadState();

    std::vector<Match> ranked = rankListings(cache.listings, profile);

    // Keep the ones worth showing.
    std::vector<const Match*> shown;
    int newCount = 0, hiddenIneligible = 0;
    for (const Match& m : ranked) {
        if (!o.all && m.ineligible()) { ++hiddenIneligible; continue; }
        if (!o.all && (m.termMismatch || m.score < o.minScore)) continue;
        if (o.type == "intern" && looksLikeCoop(m.listing->title)) continue;
        if (o.type == "coop" && !looksLikeCoop(m.listing->title)) continue;
        if (state.isNew(m.listing->id) && !o.all && m.score >= o.minScore) ++newCount;
        if (static_cast<int>(shown.size()) < o.limit) shown.push_back(&m);
    }

    bool firstRun = state.firstSeen.empty();
    std::cout << "\n" << ui::header("Top internships for " + (profile.name.empty() ? "you" : profile.name) +
                                    " (" + text::join(profile.terms, ", ") + ")") << "\n";
    if (!firstRun && !o.all)
        std::cout << ui::dim(std::to_string(newCount) + " new since your last check") << "\n";
    std::cout << "\n";

    state.lastSearch.clear();
    int i = 1;
    for (const Match* m : shown) {
        printMatchRow(i++, *m, state);
        state.lastSearch.push_back(m->listing->id);
    }
    if (shown.empty())
        std::cout << ui::warn("Nothing scored above " + std::to_string(o.minScore) +
                              ". Try `--min 20`, `--all`, or add more interests with `internscout setup`.") << "\n";
    else
        std::cout << "\n" << ui::dim("show N for details, open N to apply, save N to bookmark.  Showing " +
                                     std::to_string(shown.size()) + " of " + std::to_string(ranked.size()) + " listings" +
                                     (hiddenIneligible ? ", " + std::to_string(hiddenIneligible) +
                                      " hidden because you don't meet a stated requirement (--all shows them)" : "") + ".")
                  << "\n";

    markAllSeen(state, cache.listings);
    saveState(state);
    return 0;
}

int cmdWatch(const Options& o) {
    Profile profile = requireProfile();
    std::cout << ui::header("Watching for new internships") << "\n"
              << ui::dim("Checking every " + std::to_string(o.everyMinutes) + " minutes; matches scoring " +
                         std::to_string(o.minScore) + "+ trigger a notification. Ctrl-C to stop.") << "\n\n";

    while (true) {
        State state = loadState();
        bool firstRun = state.firstSeen.empty();
        ListingCache cache = getListings(true, /*quiet=*/true);
        std::vector<Match> ranked = rankListings(cache.listings, profile);

        std::vector<const Match*> fresh;
        for (const Match& m : ranked)
            if (state.isNew(m.listing->id) && !m.termMismatch && !m.ineligible() && m.score >= o.minScore) fresh.push_back(&m);

        std::time_t now = std::time(nullptr);
        char stamp[32];
        std::strftime(stamp, sizeof stamp, "%H:%M", localtime(&now));

        if (firstRun) {
            std::cout << ui::dim(std::string(stamp) + "  first check: remembering " + std::to_string(cache.listings.size()) +
                                 " current listings; you'll be told about anything new from now on.") << "\n";
        } else if (fresh.empty()) {
            std::cout << ui::dim(std::string(stamp) + "  nothing new (" + std::to_string(cache.listings.size()) + " listings checked)") << "\n";
        } else {
            std::cout << ui::green(std::string(stamp) + "  " + std::to_string(fresh.size()) + " new match(es)!") << "\n";
            state.lastSearch.clear();
            int i = 1;
            for (const Match* m : fresh) {
                if (i <= o.limit) printMatchRow(i, *m, state);
                state.lastSearch.push_back(m->listing->id);
                ++i;
            }
            const Listing& top = *fresh.front()->listing;
            ui::notify("InternScout: " + std::to_string(fresh.size()) + " new internship" + (fresh.size() == 1 ? "" : "s"),
                       top.company + " - " + top.title);
            std::cout << ui::dim("  (open N / save N work on these numbers)") << "\n";
        }

        markAllSeen(state, cache.listings);
        saveState(state);
        std::cout << std::flush;
        std::this_thread::sleep_for(std::chrono::minutes(std::max(1, o.everyMinutes)));
    }
    return 0;
}

int cmdShow(const Options& o) {
    if (o.positional.empty()) { std::cout << ui::fail("usage: internscout show N") << "\n"; return 1; }
    Profile profile = requireProfile();
    ListingCache cache = cachedListings();
    State state = loadState();
    const Listing* l = resolveResult(o.positional[0], state, cache.listings);
    if (!l) return 1;
    printListingDetail(rescore(*l, profile), state);
    return 0;
}

int cmdOpen(const Options& o) {
    if (o.positional.empty()) { std::cout << ui::fail("usage: internscout open N") << "\n"; return 1; }
    ListingCache cache = cachedListings();
    State state = loadState();
    const Listing* l = resolveResult(o.positional[0], state, cache.listings);
    if (!l) return 1;
    std::cout << ui::ok("Opening " + l->company + " - " + l->title) << "\n" << ui::dim(l->url) << "\n";
    ui::openInBrowser(l->url);
    return 0;
}

int cmdMark(const Options& o, const std::string& what) {
    if (o.positional.empty()) { std::cout << ui::fail("usage: internscout " + what + " N") << "\n"; return 1; }
    ListingCache cache = cachedListings();
    State state = loadState();
    const Listing* l = resolveResult(o.positional[0], state, cache.listings);
    if (!l) return 1;
    if (what == "save") { state.saved.insert(l->id); std::cout << ui::ok("Saved: " + l->company + " - " + l->title) << "\n"; }
    else if (what == "unsave") { state.saved.erase(l->id); std::cout << ui::ok("Removed bookmark: " + l->title) << "\n"; }
    else if (what == "applied") { state.applied.insert(l->id); state.saved.erase(l->id);
                                  std::cout << ui::ok("Marked as applied: " + l->company + " - " + l->title) << "\n"; }
    saveState(state);
    return 0;
}

int cmdSaved() {
    Profile profile = requireProfile();
    ListingCache cache = cachedListings();
    State state = loadState();
    std::vector<const Listing*> items;
    for (const Listing& l : cache.listings)
        if (state.saved.count(l.id) || state.applied.count(l.id)) items.push_back(&l);
    if (items.empty()) { std::cout << ui::dim("Nothing saved yet. Use `save N` after a search.") << "\n"; return 0; }

    std::cout << "\n" << ui::header("Saved & applied") << "\n\n";
    state.lastSearch.clear();
    int i = 1;
    for (const Listing* l : items) {
        printMatchRow(i++, rescore(*l, profile), state);
        state.lastSearch.push_back(l->id);
    }
    std::cout << "\n" << ui::dim("Numbers above now work with show / open / applied / unsave.") << "\n";
    saveState(state);
    return 0;
}

int cmdSources(const Options& o) {
    SourceConfig cfg = loadSourceConfig(sourcesPath());
    if (!o.positional.empty()) {  // `internscout sources deep on|off`
        std::string what = o.positional[0];
        std::string val = o.positional.size() > 1 ? text::lower(o.positional[1]) : "";
        if (what == "deep" && (val == "on" || val == "off")) {
            cfg.discover = (val == "on");
            saveSourceConfig(sourcesPath(), cfg);
            std::cout << ui::ok(std::string("Deep search ") + (cfg.discover ? "on" : "off") +
                                ". Run `internscout search --refresh` to use it.") << "\n";
            return 0;
        }
        std::cout << ui::fail("usage: internscout sources [deep on|off]") << "\n";
        return 1;
    }
    std::cout << ui::header("Sources") << "\n";
    std::cout << "  Feeds            " << cfg.feeds.size() << " community internship lists\n";
    std::cout << "  Deep search      " << (cfg.discover ? ui::green("on") : ui::dim("off"))
              << ui::dim("  (auto-discovers every company job board linked from the feeds)") << "\n";
    std::cout << "  Greenhouse       " << text::join(cfg.greenhouse, ", ") << "\n";
    std::cout << "  Ashby            " << text::join(cfg.ashby, ", ") << "\n";
    std::cout << "  Lever            " << text::join(cfg.lever, ", ") << "\n";
    std::cout << "  SmartRecruiters  " << text::join(cfg.smartrecruiters, ", ") << "\n";
    std::cout << "  Workable         " << text::join(cfg.workable, ", ") << "\n";
    std::cout << "  Workday          " << text::join(cfg.workday, ", ") << "\n\n";
    std::cout << ui::dim("Edit " + sourcesPath() + " to add companies by hand (use the slug from their careers URL).") << "\n";
    std::cout << ui::dim("`internscout sources deep off` limits the search to the feeds plus the boards listed above.") << "\n";
    return 0;
}

}  // namespace

int main(int argc, char** argv) {
    Options o = parseArgs(argc, argv);
    const std::string& c = o.command;

    if (c == "setup") return cmdSetup();
    if (c == "search" || c == "find") return cmdSearch(o);
    if (c == "watch") return cmdWatch(o);
    if (c == "show") return cmdShow(o);
    if (c == "open") return cmdOpen(o);
    if (c == "save" || c == "unsave" || c == "applied") return cmdMark(o, c);
    if (c == "saved") return cmdSaved();
    if (c == "profile") { Profile p = requireProfile(); printProfile(p); return 0; }
    if (c == "sources") return cmdSources(o);
    if (c == "help" || c == "--help" || c == "-h") { printHelp(); return 0; }

    std::cout << ui::fail("Unknown command: " + c) << "\n\n";
    printHelp();
    return 1;
}
