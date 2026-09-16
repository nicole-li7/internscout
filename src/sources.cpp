#include "sources.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <regex>
#include <set>
#include <thread>

#include "http.hpp"
#include "json.hpp"
#include "text.hpp"

using nlohmann::json;

// ---------- config ----------

static const char* kDefaultSources = R"json({
  "_comment": "Company job boards InternScout checks in addition to the Simplify feed. Board tokens are the slug in the company's careers URL, e.g. boards.greenhouse.io/<token> or jobs.ashbyhq.com/<token>.",
  "simplify": true,
  "greenhouse": [
    "stripe", "airbnb", "databricks", "anthropic", "cloudflare", "duolingo",
    "figma", "discord", "robinhood", "coinbase", "dropbox", "twitch",
    "asana", "lyft", "pinterest", "reddit", "instacart",
    "samsara", "gusto", "brex", "flexport", "janestreet"
  ],
  "ashby": [
    "notion", "openai", "ramp", "linear", "vercel", "replit", "cohere",
    "supabase", "ashby"
  ],
  "lever": [
    "kraken", "mistral"
  ]
})json";

SourceConfig loadSourceConfig(const std::string& path) {
    std::ifstream in(path);
    if (!in) {
        // First run: write the defaults so the user has something to edit.
        std::ofstream out(path);
        out << kDefaultSources;
        in.open(path);
    }
    SourceConfig cfg;
    try {
        json j;
        in >> j;
        cfg.simplify = j.value("simplify", true);
        cfg.greenhouse = j.value("greenhouse", std::vector<std::string>{});
        cfg.ashby = j.value("ashby", std::vector<std::string>{});
        cfg.lever = j.value("lever", std::vector<std::string>{});
    } catch (...) {
        // Broken file -> fall back to just the Simplify feed rather than crashing.
        cfg = SourceConfig{};
    }
    return cfg;
}

// ---------- helpers ----------

bool looksLikeInternship(const std::string& title) {
    // Word-boundary match so "International Sales" and "Internal Tools" do not count.
    static const std::regex re(R"(\b(intern|interns|internship|internships|co-op|coop|co op)\b)",
                               std::regex::icase);
    return std::regex_search(title, re);
}

bool looksLikeCoop(const std::string& title) {
    static const std::regex re(R"(\b(co-op|coop|co op|cooperative education)\b)", std::regex::icase);
    return std::regex_search(title, re);
}

std::vector<std::string> extractTerms(const std::string& textToScan) {
    static const std::regex re(R"(\b(Summer|Fall|Autumn|Winter|Spring)\s*[-/]?\s*(20\d\d)\b)",
                               std::regex::icase);
    std::set<std::string> found;
    // Only scan the first few KB: terms are almost always near the top of a posting,
    // and std::regex is slow on huge strings.
    std::string head = textToScan.substr(0, 6000);
    for (auto it = std::sregex_iterator(head.begin(), head.end(), re); it != std::sregex_iterator(); ++it) {
        std::string season = text::lower((*it)[1].str());
        season[0] = static_cast<char>(std::toupper(season[0]));
        if (season == "Autumn") season = "Fall";
        found.insert(season + " " + (*it)[2].str());
    }
    return {found.begin(), found.end()};
}

static bool isRemoteLocation(const std::string& loc) {
    return text::contains(loc, "remote");
}

static std::string getString(const json& j, const char* key) {
    auto it = j.find(key);
    if (it == j.end() || !it->is_string()) return "";
    return it->get<std::string>();
}

// ---------- Simplify (GitHub) ----------

static void fetchSimplify(std::vector<Listing>& out, FetchReport& report) {
    // The repo is renamed each year (Summer2026-Internships, Summer2027-Internships...)
    // but GitHub redirects the old name, so this URL keeps working.
    const std::string url =
        "https://raw.githubusercontent.com/SimplifyJobs/Summer2026-Internships/dev/.github/scripts/listings.json";
    HttpResult r = httpGet(url, 120);
    if (!r.ok()) { report.error = r.error; return; }

    try {
        json j = json::parse(r.body);
        for (const json& item : j) {
            // Skip closed or hidden postings.
            if (!item.value("active", false) || !item.value("is_visible", true)) continue;

            Listing l;
            l.id = "simplify:" + getString(item, "id");
            l.source = "Simplify";
            l.company = getString(item, "company_name");
            l.title = getString(item, "title");
            l.locations = item.value("locations", std::vector<std::string>{});
            l.terms = item.value("terms", std::vector<std::string>{});
            l.degrees = item.value("degrees", std::vector<std::string>{});
            l.category = getString(item, "category");
            l.sponsorship = getString(item, "sponsorship");
            l.url = getString(item, "url");
            l.datePosted = static_cast<std::time_t>(item.value("date_posted", 0LL));
            for (const auto& loc : l.locations) l.remote = l.remote || isRemoteLocation(loc);
            if (l.title.empty() || l.url.empty()) continue;
            out.push_back(std::move(l));
            ++report.fetched;
        }
    } catch (const std::exception& e) {
        report.error = std::string("bad JSON: ") + e.what();
    }
}

// ---------- Greenhouse ----------

static void fetchGreenhouse(const std::string& token, std::vector<Listing>& out, FetchReport& report) {
    const std::string url = "https://boards-api.greenhouse.io/v1/boards/" + token + "/jobs?content=true";
    HttpResult r = httpGet(url);
    if (!r.ok()) { report.error = r.error; return; }

    try {
        json j = json::parse(r.body);
        for (const json& job : j.value("jobs", json::array())) {
            std::string title = getString(job, "title");
            if (!looksLikeInternship(title)) continue;

            Listing l;
            l.id = "greenhouse:" + token + ":" + std::to_string(job.value("id", 0LL));
            l.source = "Greenhouse";
            l.company = getString(job, "company_name");
            if (l.company.empty()) l.company = token;
            l.title = title;
            if (job.contains("location") && job["location"].is_object())
                l.locations.push_back(getString(job["location"], "name"));
            // Greenhouse returns the description as HTML *escaped* inside JSON ("&lt;p&gt;...").
            l.description = text::stripHtml(text::decodeEntities(getString(job, "content")));
            l.url = getString(job, "absolute_url");
            std::string posted = getString(job, "first_published");
            if (posted.empty()) posted = getString(job, "updated_at");
            l.datePosted = text::parseIsoDate(posted);
            l.terms = extractTerms(title + " " + l.description);
            for (const auto& loc : l.locations) l.remote = l.remote || isRemoteLocation(loc);
            if (l.url.empty()) continue;
            out.push_back(std::move(l));
            ++report.fetched;
        }
    } catch (const std::exception& e) {
        report.error = std::string("bad JSON: ") + e.what();
    }
}

// ---------- Ashby ----------

static void fetchAshby(const std::string& token, std::vector<Listing>& out, FetchReport& report) {
    const std::string url = "https://api.ashbyhq.com/posting-api/job-board/" + token + "?includeCompensation=false";
    HttpResult r = httpGet(url);
    if (!r.ok()) { report.error = r.error; return; }

    try {
        json j = json::parse(r.body);
        for (const json& job : j.value("jobs", json::array())) {
            std::string title = getString(job, "title");
            std::string type = getString(job, "employmentType");  // "FullTime", "Intern", ...
            if (!looksLikeInternship(title) && text::lower(type) != "intern") continue;
            if (!job.value("isListed", true)) continue;

            Listing l;
            l.id = "ashby:" + token + ":" + getString(job, "id");
            l.source = "Ashby";
            l.company = token;
            l.title = title;
            std::string primary = getString(job, "location");
            if (!primary.empty()) l.locations.push_back(primary);
            for (const json& sec : job.value("secondaryLocations", json::array()))
                if (sec.is_object()) l.locations.push_back(getString(sec, "location"));
            l.remote = job.value("isRemote", false);
            l.description = getString(job, "descriptionPlain");
            l.url = getString(job, "jobUrl");
            l.datePosted = text::parseIsoDate(getString(job, "publishedAt"));
            l.terms = extractTerms(title + " " + l.description);
            if (l.url.empty()) continue;
            out.push_back(std::move(l));
            ++report.fetched;
        }
    } catch (const std::exception& e) {
        report.error = std::string("bad JSON: ") + e.what();
    }
}

// ---------- Lever ----------

static void fetchLever(const std::string& token, std::vector<Listing>& out, FetchReport& report) {
    const std::string url = "https://api.lever.co/v0/postings/" + token + "?mode=json";
    HttpResult r = httpGet(url);
    if (!r.ok()) { report.error = r.error; return; }

    try {
        json j = json::parse(r.body);
        if (!j.is_array()) { report.error = "board not found"; return; }
        for (const json& job : j) {
            std::string title = getString(job, "text");
            std::string commitment;
            if (job.contains("categories") && job["categories"].is_object())
                commitment = getString(job["categories"], "commitment");
            if (!looksLikeInternship(title) && !looksLikeInternship(commitment)) continue;

            Listing l;
            l.id = "lever:" + token + ":" + getString(job, "id");
            l.source = "Lever";
            l.company = token;
            l.title = title;
            if (job.contains("categories") && job["categories"].is_object()) {
                std::string loc = getString(job["categories"], "location");
                if (!loc.empty()) l.locations.push_back(loc);
            }
            l.remote = text::contains(getString(job, "workplaceType"), "remote");
            for (const auto& loc : l.locations) l.remote = l.remote || isRemoteLocation(loc);
            l.description = getString(job, "descriptionPlain");
            l.url = getString(job, "hostedUrl");
            l.datePosted = static_cast<std::time_t>(job.value("createdAt", 0LL) / 1000);  // Lever uses milliseconds
            l.terms = extractTerms(title + " " + l.description);
            if (l.url.empty()) continue;
            out.push_back(std::move(l));
            ++report.fetched;
        }
    } catch (const std::exception& e) {
        report.error = std::string("bad JSON: ") + e.what();
    }
}

// ---------- everything ----------

std::vector<Listing> fetchAllSources(const SourceConfig& config,
                                     std::vector<FetchReport>& reports,
                                     const std::function<void(const std::string&)>& progress) {
    // Build a list of "jobs" (one per source) and run them on a few threads at once.
    // Each job writes into its own slot so no locking is needed around the results.
    struct Job {
        std::string label;
        std::string reportName;
        std::function<void(std::vector<Listing>&, FetchReport&)> run;
    };
    std::vector<Job> jobs;
    if (config.simplify)
        jobs.push_back({"Simplify internship list (GitHub)", "Simplify", fetchSimplify});
    for (const auto& t : config.greenhouse)
        jobs.push_back({"Greenhouse: " + t, "Greenhouse/" + t,
                        [t](std::vector<Listing>& o, FetchReport& r) { fetchGreenhouse(t, o, r); }});
    for (const auto& t : config.ashby)
        jobs.push_back({"Ashby: " + t, "Ashby/" + t,
                        [t](std::vector<Listing>& o, FetchReport& r) { fetchAshby(t, o, r); }});
    for (const auto& t : config.lever)
        jobs.push_back({"Lever: " + t, "Lever/" + t,
                        [t](std::vector<Listing>& o, FetchReport& r) { fetchLever(t, o, r); }});

    std::vector<std::vector<Listing>> results(jobs.size());
    reports.assign(jobs.size(), FetchReport{});
    for (size_t i = 0; i < jobs.size(); ++i) reports[i].name = jobs[i].reportName;

    std::atomic<size_t> nextJob{0};
    std::mutex progressMutex;
    auto worker = [&] {
        while (true) {
            size_t i = nextJob++;
            if (i >= jobs.size()) return;
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                progress(jobs[i].label);
            }
            jobs[i].run(results[i], reports[i]);
        }
    };
    const size_t threadCount = std::min<size_t>(8, jobs.size());
    std::vector<std::thread> threads;
    for (size_t i = 0; i < threadCount; ++i) threads.emplace_back(worker);
    for (auto& th : threads) th.join();

    std::vector<Listing> all;
    for (auto& r : results) all.insert(all.end(), r.begin(), r.end());

    // De-duplicate: the same posting can appear in Simplify *and* on the company's own board.
    // Prefer the company-board version because it has a description.
    std::set<std::string> seenUrls;
    std::vector<Listing> deduped;
    // Company boards come after Simplify in `all`, so walk backwards to keep the later ones.
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        std::string key = text::lower(it->url);
        // Strip tracking query strings so "?gh_src=..." variants collapse together.
        size_t q = key.find('?');
        if (q != std::string::npos && key.find("gh_jid") == std::string::npos) key = key.substr(0, q);
        if (!seenUrls.insert(key).second) continue;
        deduped.push_back(*it);
    }
    return {deduped.rbegin(), deduped.rend()};
}
