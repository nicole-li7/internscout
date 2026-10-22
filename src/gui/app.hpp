// app.hpp - the windowed InternScout app.
//
// One App object owns everything on screen: the profile form, the fetched
// listings, the ranked results, and the background download thread.
// main_gui.cpp creates the window and calls App::frame() sixty times a second;
// frame() draws the whole UI with Dear ImGui (an "immediate mode" GUI, which
// means every frame we describe the UI from scratch and it handles the rest).
#pragma once

#include <atomic>
#include <ctime>
#include <future>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>

#include "listing.hpp"
#include "matcher.hpp"
#include "profile.hpp"
#include "sources.hpp"
#include "store.hpp"

class App {
public:
    App();
    ~App();

    // Draw one frame of the UI. Called every frame by main_gui.cpp.
    void frame();

    // Set by main_gui.cpp: fonts loaded at startup.
    struct ImFont* bodyFont = nullptr;
    struct ImFont* headingFont = nullptr;

private:
    // ----- pages -----
    void drawProfilePage();
    void drawResultsPage();
    void drawSavedPage();
    void drawAppliedPage();
    void drawOutcomeChart();    // Applied tab: how your applications turned out
    void drawScoringPage();
    void drawToolbar();
    void drawTable(const std::vector<int>& rows, const char* tableId);
    void drawDetail();

    // ----- profile form <-> Profile -----
    void loadFormFromProfile();
    Profile profileFromForm() const;
    void saveProfileFromForm();

    // ----- data -----
    void startFetch();          // kick off the background download (it ranks the new listings too)
    void pollFetch();           // called every frame: has the download finished?
    void rerank();              // score listings against the profile, on a worker thread
    void pollRank();            // called every frame: has the ranking finished?
    // Show a finished ranking. `ranked` points into `listings`, which become listings_
    // (moving a vector keeps its elements in place, so the pointers stay valid).
    // With `reconcile`, saved/applied listings are pinned and closed ones appended and scored.
    void installRanking(std::vector<Listing> listings, std::vector<Match> ranked, const Profile& rankedWith,
                        int profileVersion, bool reconcile);
    void refilter();            // apply the toolbar filters to the ranked list
    void maybeAutoRefresh();

    // ----- persistent things -----
    Profile profile_;
    bool profileOk_ = false;
    State state_;
    std::vector<Listing> listings_;
    std::time_t fetchedAt_ = 0;

    // ----- results -----
    std::vector<Match> ranked_;          // every listing, best first
    std::vector<int> visible_;           // indexes into ranked_ that pass the filters
    std::vector<int> savedRows_;         // indexes into ranked_ that are bookmarked
    std::vector<int> appliedRows_;       // indexes into ranked_ that are marked applied
    std::set<std::string> newIds_;       // listings first seen during this session
    int selected_ = -1;                  // index into ranked_ of the highlighted row
    bool showChart_ = false;             // Applied tab: chart instead of the list
    bool filtersDirty_ = true;

    // ----- toolbar filters -----
    char filterText_[128] = "";
    int minScore_ = 40;
    bool allTerms_ = false;
    bool hideIneligible_ = true;         // hide listings whose stated requirements you do not meet
    int hiddenIneligible_ = 0;
    int typeIdx_ = 0;                    // 0 = all, 1 = internships only, 2 = co-ops only
    int autoRefreshIdx_ = 4;             // index into kAutoRefreshMinutes (every 2 hours: a deep search checks ~2,400 boards)
    bool deepSearch_ = true;             // mirrors "discover" in sources.json
    std::string lastNotice_;             // one-line status shown in the toolbar

    // ----- profile form state -----
    char nameBuf_[96] = "";
    int universityIdx_ = 0;
    char otherUniversity_[128] = "";
    int majorIdx_ = 0;
    char otherMajor_[128] = "";
    int degreeIdx_ = 0;
    int yearIdx_ = 0;
    int gradIdx_ = 2;
    int countryIdx_ = 0;
    char otherCountry_[64] = "";
    bool anyInterests_ = false;
    std::set<std::string> selectedInterests_;
    std::vector<std::string> customInterests_;
    char newInterest_[64] = "";
    bool anywhere_ = false;
    std::set<std::string> selectedLocations_;
    std::vector<std::string> customLocations_;
    char newLocation_[64] = "";
    std::set<std::string> selectedTerms_;
    bool remoteOk_ = true;
    bool needsSponsorship_ = false;
    std::string formMessage_;

    // ----- background fetch -----
    std::thread fetchThread_;
    std::atomic<bool> fetching_{false};
    std::atomic<bool> fetchDone_{false};
    std::mutex fetchMutex_;
    std::string fetchStatus_;
    std::vector<Listing> fetchResult_;
    std::vector<Match> fetchRanked_;     // fetchResult_ ranked on the fetch thread (points into it)
    std::vector<FetchReport> fetchReports_;
    std::time_t fetchFinishedAt_ = 0;
    Profile fetchProfile_;               // the profile the fetch thread ranked with
    int fetchProfileVersion_ = 0;
    bool fetchWasAutomatic_ = false;

    // ----- background ranking (startup cache load and profile saves) -----
    struct RankJob {
        bool cacheLoad = false;          // startup: the listings come from listings.json
        std::time_t fetchedAt = 0;       // cacheLoad only
        std::vector<Listing> listings;   // what was ranked
        std::vector<Match> ranked;       // points into `listings`
        Profile profile;
        int profileVersion = 0;
        int listingsVersion = 0;
    };
    std::future<RankJob> rankJob_;       // valid() while a ranking is running
    int profileVersion_ = 0;             // bumped by every profile save
    int rankedProfileVersion_ = -1;      // the profile version ranked_ was made with
    int listingsVersion_ = 0;            // bumped whenever listings_ is replaced (cache load, refresh)

    // which tab to show next frame (-1 = leave as is)
    int requestTab_ = -1;
};
