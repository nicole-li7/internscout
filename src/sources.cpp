#include "sources.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <fstream>
#include <map>
#include <mutex>
#include <regex>
#include <set>
#include <thread>

#include "http.hpp"
#include "json.hpp"
#include "text.hpp"

using nlohmann::json;

// ---------- config ----------

static const std::vector<std::string> kDefaultFeeds = {
    // The Simplify repo is renamed each year; GitHub redirects old names, so both keep working.
    "https://raw.githubusercontent.com/SimplifyJobs/Summer2027-Internships/dev/.github/scripts/listings.json",
    "https://raw.githubusercontent.com/vanshb03/Summer2026-Internships/dev/.github/scripts/listings.json",
};

static const char* kDefaultSources = R"json({
  "_comment": "Where InternScout looks. 'feeds' are community internship lists. With 'discover' on, every company job board linked from those feeds is queried directly too (hundreds of companies). The lists below add boards by hand: use the slug from the company's careers URL, e.g. boards.greenhouse.io/<token>, jobs.ashbyhq.com/<token>, jobs.lever.co/<token>, jobs.smartrecruiters.com/<token>, apply.workable.com/<token>, or 'tenant.wd5.myworkdayjobs.com/SiteName' for Workday.",
  "feeds": [
    "https://raw.githubusercontent.com/SimplifyJobs/Summer2027-Internships/dev/.github/scripts/listings.json",
    "https://raw.githubusercontent.com/vanshb03/Summer2026-Internships/dev/.github/scripts/listings.json"
  ],
  "discover": true,
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
  ],
  "smartrecruiters": [],
  "workable": [],
  "workday": []
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
        cfg.feeds = j.value("feeds", kDefaultFeeds);
        cfg.discover = j.value("discover", true);
        cfg.greenhouse = j.value("greenhouse", std::vector<std::string>{});
        cfg.ashby = j.value("ashby", std::vector<std::string>{});
        cfg.lever = j.value("lever", std::vector<std::string>{});
        cfg.smartrecruiters = j.value("smartrecruiters", std::vector<std::string>{});
        cfg.workable = j.value("workable", std::vector<std::string>{});
        cfg.workday = j.value("workday", std::vector<std::string>{});
        // Older config files (before the feeds/discover keys existed) had only "simplify".
        if (!j.contains("feeds")) cfg.feeds = kDefaultFeeds;
    } catch (...) {
        // Broken file -> fall back to defaults rather than crashing.
        cfg = SourceConfig{};
        cfg.feeds = kDefaultFeeds;
    }
    return cfg;
}

void saveSourceConfig(const std::string& path, const SourceConfig& cfg) {
    json j;
    std::ifstream in(path);
    if (in) { try { in >> j; } catch (...) { j = json::object(); } }
    if (!j.is_object()) j = json::object();
    j["feeds"] = cfg.feeds;
    j["discover"] = cfg.discover;
    j["greenhouse"] = cfg.greenhouse;
    j["ashby"] = cfg.ashby;
    j["lever"] = cfg.lever;
    j["smartrecruiters"] = cfg.smartrecruiters;
    j["workable"] = cfg.workable;
    j["workday"] = cfg.workday;
    std::ofstream out(path);
    out << j.dump(2) << "\n";
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

static void finishListing(Listing& l) {
    for (const auto& loc : l.locations) l.remote = l.remote || isRemoteLocation(loc);
}

// ---------- feeds (Simplify-format JSON lists) ----------

// `allUrls` collects the link of *every* entry, including closed ones, because
// even a closed posting tells us which company job board to go and check.
// Each entry of `allUrls` is "Company Name\turl" so discovery can also learn the proper company name.
static void fetchFeed(const std::string& url, const std::string& sourceName, std::vector<Listing>& out,
                      FetchReport& report, std::vector<std::string>& allUrls) {
    HttpResult r = httpGet(url, 120);
    if (!r.ok()) { report.error = r.error; return; }

    try {
        json j = json::parse(r.body);
        for (const json& item : j) {
            std::string link = getString(item, "url");
            if (!link.empty()) allUrls.push_back(getString(item, "company_name") + "\t" + link);

            // Skip closed or hidden postings.
            if (!item.value("active", false) || !item.value("is_visible", true)) continue;

            Listing l;
            l.id = "simplify:" + getString(item, "id");
            l.source = sourceName;
            l.company = getString(item, "company_name");
            l.title = getString(item, "title");
            l.locations = item.value("locations", std::vector<std::string>{});
            l.terms = item.value("terms", std::vector<std::string>{});
            if (l.terms.empty()) {  // the smaller list uses a single "season" string instead
                std::string season = getString(item, "season");
                if (!season.empty()) l.terms.push_back(season);
            }
            l.degrees = item.value("degrees", std::vector<std::string>{});
            l.category = getString(item, "category");
            l.sponsorship = getString(item, "sponsorship");
            l.url = link;
            l.datePosted = static_cast<std::time_t>(item.value("date_posted", 0LL));
            finishListing(l);
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
    HttpResult r = httpGet(url, 30);
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
            finishListing(l);
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
    HttpResult r = httpGet(url, 30);
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
            finishListing(l);
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
    HttpResult r = httpGet(url, 30);
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
            l.description = getString(job, "descriptionPlain");
            l.url = getString(job, "hostedUrl");
            l.datePosted = static_cast<std::time_t>(job.value("createdAt", 0LL) / 1000);  // Lever uses milliseconds
            l.terms = extractTerms(title + " " + l.description);
            finishListing(l);
            if (l.url.empty()) continue;
            out.push_back(std::move(l));
            ++report.fetched;
        }
    } catch (const std::exception& e) {
        report.error = std::string("bad JSON: ") + e.what();
    }
}

// ---------- SmartRecruiters ----------

static void fetchSmartRecruiters(const std::string& token, std::vector<Listing>& out, FetchReport& report) {
    // Keyword search, 100 per page, at most 3 pages per company.
    for (int page = 0; page < 3; ++page) {
        const std::string url = "https://api.smartrecruiters.com/v1/companies/" + token +
                                "/postings?q=intern&limit=100&offset=" + std::to_string(page * 100);
        HttpResult r = httpGet(url, 30);
        if (!r.ok()) { if (page == 0) report.error = r.error; return; }
        try {
            json j = json::parse(r.body);
            const json& content = j.value("content", json::array());
            for (const json& job : content) {
                std::string title = getString(job, "name");
                if (!looksLikeInternship(title)) continue;
                Listing l;
                l.id = "smartrecruiters:" + token + ":" + getString(job, "id");
                l.source = "SmartRecruiters";
                l.company = (job.contains("company") && job["company"].is_object()) ? getString(job["company"], "name") : token;
                if (l.company.empty()) l.company = token;
                l.title = title;
                if (job.contains("location") && job["location"].is_object()) {
                    const json& loc = job["location"];
                    std::string full = getString(loc, "fullLocation");
                    if (full.empty()) full = text::join({getString(loc, "city"), getString(loc, "region"), getString(loc, "country")}, ", ");
                    if (!full.empty()) l.locations.push_back(full);
                    l.remote = loc.value("remote", false);
                }
                l.url = "https://jobs.smartrecruiters.com/" + token + "/" + getString(job, "id");
                l.datePosted = text::parseIsoDate(getString(job, "releasedDate"));
                l.terms = extractTerms(title);
                finishListing(l);
                out.push_back(std::move(l));
                ++report.fetched;
            }
            if (content.size() < 100) return;  // last page
        } catch (const std::exception& e) {
            report.error = std::string("bad JSON: ") + e.what();
            return;
        }
    }
}

// ---------- Workable ----------

static void fetchWorkable(const std::string& token, std::vector<Listing>& out, FetchReport& report) {
    const std::string url = "https://apply.workable.com/api/v3/accounts/" + token + "/jobs";
    HttpResult r = httpPost(url, R"({"query":"intern","location":[],"department":[],"worktype":[],"remote":[]})", 30);
    if (!r.ok()) { report.error = r.error; return; }
    try {
        json j = json::parse(r.body);
        for (const json& job : j.value("results", json::array())) {
            std::string title = getString(job, "title");
            if (!looksLikeInternship(title)) continue;
            Listing l;
            l.id = "workable:" + token + ":" + getString(job, "shortcode");
            l.source = "Workable";
            l.company = token;
            l.title = title;
            if (job.contains("location") && job["location"].is_object()) {
                const json& loc = job["location"];
                std::string s = text::join({getString(loc, "city"), getString(loc, "region"), getString(loc, "country")}, ", ");
                if (!s.empty()) l.locations.push_back(s);
            }
            l.remote = job.value("remote", false);
            l.url = "https://apply.workable.com/" + token + "/j/" + getString(job, "shortcode") + "/";
            l.datePosted = text::parseIsoDate(getString(job, "published"));
            l.terms = extractTerms(title);
            finishListing(l);
            out.push_back(std::move(l));
            ++report.fetched;
        }
    } catch (const std::exception& e) {
        report.error = std::string("bad JSON: ") + e.what();
    }
}

// ---------- Workday ----------

// Workday says "Posted Today", "Posted 3 Days Ago", "Posted 30+ Days Ago"; turn that into a date.
static std::time_t workdayDate(const std::string& postedOn) {
    std::string s = text::lower(postedOn);
    int days = 0;
    if (s.find("today") != std::string::npos) days = 0;
    else if (s.find("yesterday") != std::string::npos) days = 1;
    else if (s.find("30+") != std::string::npos) days = 31;
    else {
        int n = 0;
        if (std::sscanf(s.c_str(), "posted %d", &n) == 1) days = n;
        else return 0;
    }
    return std::time(nullptr) - static_cast<std::time_t>(days) * 24 * 60 * 60;
}

// `site` looks like "nvidia.wd5.myworkdayjobs.com/NVIDIAExternalCareerSite".
static void fetchWorkday(const std::string& site, std::vector<Listing>& out, FetchReport& report) {
    size_t slash = site.find('/');
    size_t dot = site.find('.');
    if (slash == std::string::npos || dot == std::string::npos) { report.error = "bad workday site"; return; }
    const std::string host = site.substr(0, slash);
    const std::string tenant = site.substr(0, dot);
    const std::string siteName = site.substr(slash + 1);
    const std::string api = "https://" + host + "/wday/cxs/" + tenant + "/" + siteName + "/jobs";

    const int pageSize = 20;
    for (int page = 0; page < 5; ++page) {  // at most 100 postings per site
        std::string body = R"({"appliedFacets":{},"limit":20,"offset":)" + std::to_string(page * pageSize) +
                           R"(,"searchText":"intern"})";
        HttpResult r = httpPost(api, body, 30);
        if (!r.ok()) { if (page == 0) report.error = r.error; return; }
        try {
            json j = json::parse(r.body);
            const json& posts = j.value("jobPostings", json::array());
            for (const json& job : posts) {
                std::string title = getString(job, "title");
                if (!looksLikeInternship(title)) continue;
                std::string path = getString(job, "externalPath");
                if (path.empty()) continue;
                Listing l;
                l.id = "workday:" + site + ":" + path;
                l.source = "Workday";
                l.company = tenant;
                l.title = title;
                std::string loc = getString(job, "locationsText");
                if (!loc.empty()) l.locations.push_back(loc);
                l.url = "https://" + host + "/" + siteName + path;
                l.datePosted = workdayDate(getString(job, "postedOn"));
                l.terms = extractTerms(title);
                finishListing(l);
                out.push_back(std::move(l));
                ++report.fetched;
            }
            int total = j.value("total", 0);
            if (static_cast<int>(posts.size()) < pageSize || (page + 1) * pageSize >= total) return;
        } catch (const std::exception& e) {
            report.error = std::string("bad JSON: ") + e.what();
            return;
        }
    }
}

// ---------- discovery ----------

// Look at every link the feeds gave us and work out which company job boards
// they point at, so we can query those boards directly.
// `names` maps a board slug (lower-case) to the company's proper name, e.g. "plastipak" -> "Plastipak".
static void discoverBoards(const std::vector<std::string>& entries, SourceConfig& cfg,
                           std::map<std::string, std::string>& names) {
    static const std::regex greenhouse(R"((?:boards|job-boards)\.greenhouse\.io/([A-Za-z0-9_-]+))");
    static const std::regex lever(R"(jobs\.lever\.co/([A-Za-z0-9_-]+))");
    static const std::regex ashby(R"(jobs\.ashbyhq\.com/([A-Za-z0-9_.-]+))");
    static const std::regex smart(R"(jobs\.smartrecruiters\.com/([A-Za-z0-9_-]+))");
    static const std::regex workable(R"(apply\.workable\.com/([A-Za-z0-9_-]+))");
    static const std::regex workday(R"(https?://([a-z0-9-]+\.wd\d+\.myworkdayjobs\.com)/(?:[a-z]{2}-[A-Za-z]{2}/)?([^/?#]+))");

    std::set<std::string> gh(cfg.greenhouse.begin(), cfg.greenhouse.end());
    std::set<std::string> lv(cfg.lever.begin(), cfg.lever.end());
    std::set<std::string> ab(cfg.ashby.begin(), cfg.ashby.end());
    std::set<std::string> sr(cfg.smartrecruiters.begin(), cfg.smartrecruiters.end());
    std::set<std::string> wk(cfg.workable.begin(), cfg.workable.end());
    std::set<std::string> wd(cfg.workday.begin(), cfg.workday.end());

    std::smatch m;
    for (const std::string& entry : entries) {
        size_t tab = entry.find('\t');
        std::string company = tab == std::string::npos ? "" : entry.substr(0, tab);
        std::string u = tab == std::string::npos ? entry : entry.substr(tab + 1);
        std::string slug;
        if (std::regex_search(u, m, greenhouse)) { gh.insert(m[1]); slug = m[1]; }
        else if (std::regex_search(u, m, lever)) { lv.insert(m[1]); slug = m[1]; }
        else if (std::regex_search(u, m, ashby)) { ab.insert(m[1]); slug = m[1]; }
        else if (std::regex_search(u, m, smart)) { sr.insert(m[1]); slug = m[1]; }
        else if (std::regex_search(u, m, workable)) { wk.insert(m[1]); slug = m[1]; }
        else if (std::regex_search(u, m, workday)) {
            std::string host = m[1], site = m[2];
            if (site != "job" && site != "wday") wd.insert(host + "/" + site);
            slug = host.substr(0, host.find('.'));  // the Workday tenant, e.g. "nvidia"
        }
        if (!slug.empty() && !company.empty()) names.emplace(text::lower(slug), company);
    }
    cfg.greenhouse.assign(gh.begin(), gh.end());
    cfg.lever.assign(lv.begin(), lv.end());
    cfg.ashby.assign(ab.begin(), ab.end());
    cfg.smartrecruiters.assign(sr.begin(), sr.end());
    cfg.workable.assign(wk.begin(), wk.end());
    cfg.workday.assign(wd.begin(), wd.end());
}

// The same posting can reach us from a feed *and* from the company's own board,
// often with slightly different links. Reduce a link to something stable.
std::string dedupeKey(const std::string& rawUrl) {
    std::string u = text::lower(rawUrl);
    size_t q = u.find('?');
    std::string query = q == std::string::npos ? "" : u.substr(q);
    if (q != std::string::npos) u = u.substr(0, q);
    if (!u.empty() && u.back() == '/') u.pop_back();

    std::smatch m;
    static const std::regex ghId(R"(greenhouse\.io/([a-z0-9_-]+)/jobs/(\d+))");
    static const std::regex ghJid(R"(gh_jid=(\d+))");
    static const std::regex leverId(R"(lever\.co/[a-z0-9_-]+/([0-9a-f-]{36}))");
    static const std::regex ashbyId(R"(ashbyhq\.com/[a-z0-9_.-]+/([0-9a-f-]{36}))");
    static const std::regex wdPath(R"(myworkdayjobs\.com/(?:[a-z]{2}-[a-z]{2}/)?[^/]+(/job/.*))");
    if (std::regex_search(u, m, ghId)) return "gh:" + std::string(m[2]);
    if (std::regex_search(query, m, ghJid)) return "gh:" + std::string(m[1]);
    if (std::regex_search(u, m, leverId)) return "lever:" + std::string(m[1]);
    if (std::regex_search(u, m, ashbyId)) return "ashby:" + std::string(m[1]);
    if (std::regex_search(u, m, wdPath)) return "wd:" + std::string(m[1]);
    return u;
}

// ---------- everything ----------

namespace {

struct Job {
    std::string label;
    std::string reportName;
    std::function<void(std::vector<Listing>&, FetchReport&)> run;
};

// Run all jobs on a pool of threads; results land in `out` in job order.
void runJobs(const std::vector<Job>& jobs, const std::string& phaseLabel, std::vector<Listing>& out,
             std::vector<FetchReport>& reports, const std::function<void(const std::string&)>& progress) {
    std::vector<std::vector<Listing>> results(jobs.size());
    std::vector<FetchReport> jobReports(jobs.size());
    for (size_t i = 0; i < jobs.size(); ++i) jobReports[i].name = jobs[i].reportName;

    std::atomic<size_t> nextJob{0};
    std::atomic<size_t> finished{0};
    std::mutex progressMutex;
    auto worker = [&] {
        while (true) {
            size_t i = nextJob++;
            if (i >= jobs.size()) return;
            {
                std::lock_guard<std::mutex> lock(progressMutex);
                progress(phaseLabel + " " + std::to_string(finished.load()) + "/" + std::to_string(jobs.size()) +
                         "  (" + jobs[i].label + ")");
            }
            jobs[i].run(results[i], jobReports[i]);
            // A board that fails for a reason other than "not found" (timeout, rate limit)
            // usually works on a second try a moment later.
            if (!jobReports[i].error.empty() && jobReports[i].error.find("404") == std::string::npos) {
                std::this_thread::sleep_for(std::chrono::seconds(2));
                results[i].clear();
                jobReports[i] = FetchReport{};
                jobReports[i].name = jobs[i].reportName;
                jobs[i].run(results[i], jobReports[i]);
            }
            ++finished;
        }
    };
    const size_t threadCount = std::min<size_t>(16, jobs.size());  // network-bound, so many threads is fine
    std::vector<std::thread> threads;
    for (size_t i = 0; i < threadCount; ++i) threads.emplace_back(worker);
    for (auto& th : threads) th.join();

    for (auto& r : results) out.insert(out.end(), r.begin(), r.end());
    reports.insert(reports.end(), jobReports.begin(), jobReports.end());
}

}  // namespace

std::vector<Listing> fetchAllSources(const SourceConfig& configIn,
                                     std::vector<FetchReport>& reports,
                                     const std::function<void(const std::string&)>& progress) {
    SourceConfig config = configIn;
    std::vector<Listing> all;

    // Phase 1: the feeds. Also collect every link they contain for discovery.
    std::vector<std::string> feedUrls;
    std::mutex feedUrlMutex;
    std::vector<Job> feedJobs;
    for (size_t i = 0; i < config.feeds.size(); ++i) {
        std::string url = config.feeds[i];
        std::string name = url.find("SimplifyJobs") != std::string::npos ? "Simplify" : "Community list";
        feedJobs.push_back({name, "Feed/" + name, [url, name, &feedUrls, &feedUrlMutex](std::vector<Listing>& o, FetchReport& r) {
            std::vector<std::string> urls;
            fetchFeed(url, name, o, r, urls);
            std::lock_guard<std::mutex> lock(feedUrlMutex);
            feedUrls.insert(feedUrls.end(), urls.begin(), urls.end());
        }});
    }
    runJobs(feedJobs, "Downloading internship lists", all, reports, progress);

    // Phase 2: company boards - the hand-picked ones plus everything discovered.
    std::map<std::string, std::string> names;
    if (config.discover) {
        progress("Finding company job boards in " + std::to_string(feedUrls.size()) + " links...");
        discoverBoards(feedUrls, config, names);
    }
    std::vector<Job> boardJobs;
    for (const auto& t : config.greenhouse)
        boardJobs.push_back({t, "Greenhouse/" + t, [t](std::vector<Listing>& o, FetchReport& r) { fetchGreenhouse(t, o, r); }});
    for (const auto& t : config.ashby)
        boardJobs.push_back({t, "Ashby/" + t, [t](std::vector<Listing>& o, FetchReport& r) { fetchAshby(t, o, r); }});
    for (const auto& t : config.lever)
        boardJobs.push_back({t, "Lever/" + t, [t](std::vector<Listing>& o, FetchReport& r) { fetchLever(t, o, r); }});
    for (const auto& t : config.smartrecruiters)
        boardJobs.push_back({t, "SmartRecruiters/" + t, [t](std::vector<Listing>& o, FetchReport& r) { fetchSmartRecruiters(t, o, r); }});
    for (const auto& t : config.workable)
        boardJobs.push_back({t, "Workable/" + t, [t](std::vector<Listing>& o, FetchReport& r) { fetchWorkable(t, o, r); }});
    for (const auto& t : config.workday)
        boardJobs.push_back({t.substr(0, t.find('.')), "Workday/" + t, [t](std::vector<Listing>& o, FetchReport& r) { fetchWorkday(t, o, r); }});
    runJobs(boardJobs, "Checking company job boards", all, reports, progress);

    // Boards only know their URL slug ("plastipak"); use the proper name the feed had for them.
    for (Listing& l : all) {
        auto it = names.find(text::lower(l.company));
        if (it != names.end()) l.company = it->second;
    }

    // De-duplicate. Company boards come after the feeds in `all`, so walk backwards
    // to keep the board version (it usually has a description).
    std::set<std::string> seen;
    std::vector<Listing> deduped;
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        if (!seen.insert(dedupeKey(it->url)).second) continue;
        deduped.push_back(*it);
    }
    return {deduped.rbegin(), deduped.rend()};
}
