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
    void drawScoringPage();
    void drawToolbar();
    void drawTable(const std::vector<int>& rows, const char* tableId);
    void drawDetail();

    // ----- profile form <-> Profile -----
    void loadFormFromProfile();
    Profile profileFromForm() const;
    void saveProfileFromForm();

    // ----- data -----
    void startFetch();          // kick off the background download
    void pollFetch();           // called every frame: has the download finished?
    void rerank();              // score listings against the profile
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
    bool filtersDirty_ = true;

    // ----- toolbar filters -----
    char filterText_[128] = "";
    int minScore_ = 40;
    bool allTerms_ = false;
    int typeIdx_ = 0;                    // 0 = all, 1 = internships only, 2 = co-ops only
    int autoRefreshIdx_ = 2;             // index into kAutoRefreshMinutes
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
    std::vector<FetchReport> fetchReports_;
    bool fetchWasAutomatic_ = false;

    // which tab to show next frame (-1 = leave as is)
    int requestTab_ = -1;
};
