#include "app.hpp"

#include <algorithm>
#include <cstring>

#include "imgui.h"
#include "options.hpp"
#include "text.hpp"
#include "ui.hpp"

namespace {

const int kAutoRefreshMinutes[] = {0, 15, 30, 60, 120};
const char* kAutoRefreshLabels[] = {"Off", "Every 15 min", "Every 30 min", "Every hour", "Every 2 hours"};

// Colours used for badges and scores.
const ImVec4 kGreen(0.13f, 0.60f, 0.33f, 1.0f);
const ImVec4 kAmber(0.80f, 0.55f, 0.10f, 1.0f);
const ImVec4 kBlue(0.16f, 0.45f, 0.85f, 1.0f);
const ImVec4 kPurple(0.55f, 0.30f, 0.75f, 1.0f);
const ImVec4 kMuted(0.45f, 0.47f, 0.52f, 1.0f);

int indexOf(const std::vector<std::string>& list, const std::string& value) {
    for (size_t i = 0; i < list.size(); ++i)
        if (list[i] == value) return static_cast<int>(i);
    return -1;
}

void copyTo(char* buf, size_t size, const std::string& s) {
    std::strncpy(buf, s.c_str(), size - 1);
    buf[size - 1] = '\0';
}

// A dropdown over a list of strings. Returns true when the choice changed.
bool combo(const char* label, int& index, const std::vector<std::string>& items) {
    bool changed = false;
    const char* preview = (index >= 0 && index < static_cast<int>(items.size())) ? items[index].c_str() : "";
    if (ImGui::BeginCombo(label, preview)) {
        for (int i = 0; i < static_cast<int>(items.size()); ++i) {
            bool selected = (i == index);
            if (ImGui::Selectable(items[i].c_str(), selected)) { index = i; changed = true; }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    return changed;
}

// A grid of checkboxes over a list of labels, with an "Any" master switch.
// Returns true if anything changed.
bool multiSelect(const char* id, const char* anyLabel, bool& any, const std::vector<std::string>& items,
                 const std::vector<std::string>& custom, std::set<std::string>& selected, int columns) {
    bool changed = false;
    ImGui::PushID(id);
    if (ImGui::Checkbox(anyLabel, &any)) changed = true;
    ImGui::BeginDisabled(any);
    std::vector<std::string> all = items;
    all.insert(all.end(), custom.begin(), custom.end());
    if (ImGui::BeginTable("grid", columns, ImGuiTableFlags_SizingStretchSame)) {
        for (const std::string& item : all) {
            ImGui::TableNextColumn();
            bool on = selected.count(item) > 0;
            if (ImGui::Checkbox(item.c_str(), &on)) {
                if (on) selected.insert(item); else selected.erase(item);
                changed = true;
            }
        }
        ImGui::EndTable();
    }
    ImGui::EndDisabled();
    ImGui::PopID();
    return changed;
}

}  // namespace

// ============================================================================
// setup / teardown
// ============================================================================

App::App() {
    profileOk_ = loadProfile(profilePath(), profile_) && profile_.isComplete();
    state_ = loadState();
    loadFormFromProfile();

    ListingCache cache;
    if (loadCache(cache)) {
        listings_ = std::move(cache.listings);
        fetchedAt_ = cache.fetchedAt;
        rerank();
    }
    if (!profileOk_) {
        requestTab_ = 0;
        formMessage_ = "Welcome! Fill this in once and InternScout will rank internships for you.";
    } else {
        requestTab_ = 1;
        if (listings_.empty() || std::time(nullptr) - fetchedAt_ > 6 * 60 * 60) startFetch();
    }
}

App::~App() {
    if (fetchThread_.joinable()) fetchThread_.join();
}

// ============================================================================
// profile form
// ============================================================================

void App::loadFormFromProfile() {
    const Profile& p = profile_;
    copyTo(nameBuf_, sizeof nameBuf_, p.name);

    const auto& unis = options::universities();
    universityIdx_ = indexOf(unis, p.school);
    if (universityIdx_ < 0) {
        universityIdx_ = p.school.empty() ? 0 : static_cast<int>(unis.size()) - 1;  // "Other"
        copyTo(otherUniversity_, sizeof otherUniversity_, p.school);
    }
    const auto& majors = options::majors();
    majorIdx_ = indexOf(majors, p.major);
    if (majorIdx_ < 0) {
        majorIdx_ = p.major.empty() ? 0 : static_cast<int>(majors.size()) - 1;
        copyTo(otherMajor_, sizeof otherMajor_, p.major);
    }
    degreeIdx_ = std::max(0, indexOf(options::degreeLevels(), p.level));
    yearIdx_ = std::clamp(p.yearOfStudy - 1, 0, static_cast<int>(options::yearsOfStudy().size()) - 1);

    std::vector<int> years = options::graduationYears();
    gradIdx_ = 2;
    for (size_t i = 0; i < years.size(); ++i)
        if (years[i] == p.gradYear) gradIdx_ = static_cast<int>(i);

    const auto& countries = options::countries();
    countryIdx_ = indexOf(countries, p.country);
    if (countryIdx_ < 0) {
        countryIdx_ = static_cast<int>(countries.size()) - 1;
        copyTo(otherCountry_, sizeof otherCountry_, p.country);
    }

    selectedInterests_.clear();
    customInterests_.clear();
    for (const std::string& label : p.interests) {
        selectedInterests_.insert(label);
        if (indexOf(options::interests(), label) < 0) customInterests_.push_back(label);
    }
    anyInterests_ = profileOk_ && p.interests.empty() && p.keywords.empty();

    selectedLocations_.clear();
    customLocations_.clear();
    for (const std::string& label : p.locations) {
        selectedLocations_.insert(label);
        if (indexOf(options::locations(), label) < 0) customLocations_.push_back(label);
    }
    anywhere_ = profileOk_ && p.locations.empty();

    selectedTerms_.clear();
    for (const std::string& t : (p.terms.empty() ? suggestTerms() : p.terms)) selectedTerms_.insert(t);

    remoteOk_ = p.remoteOk;
    needsSponsorship_ = p.needsSponsorship;
}

Profile App::profileFromForm() const {
    Profile p = profile_;  // keep fields the form does not edit (free-text keywords)
    p.name = text::trim(nameBuf_);

    const auto& unis = options::universities();
    p.school = (universityIdx_ == static_cast<int>(unis.size()) - 1) ? text::trim(otherUniversity_) : unis[universityIdx_];
    const auto& majors = options::majors();
    p.major = (majorIdx_ == static_cast<int>(majors.size()) - 1) ? text::trim(otherMajor_) : majors[majorIdx_];
    p.level = options::degreeLevels()[degreeIdx_];
    p.yearOfStudy = yearIdx_ + 1;
    p.gradYear = options::graduationYears()[gradIdx_];
    const auto& countries = options::countries();
    p.country = (countryIdx_ == static_cast<int>(countries.size()) - 1) ? text::trim(otherCountry_) : countries[countryIdx_];

    p.interests.clear();
    if (!anyInterests_) p.interests.assign(selectedInterests_.begin(), selectedInterests_.end());
    if (anyInterests_) p.keywords.clear();

    p.locations.clear();
    if (!anywhere_) p.locations.assign(selectedLocations_.begin(), selectedLocations_.end());

    p.terms.assign(selectedTerms_.begin(), selectedTerms_.end());
    if (p.terms.empty()) p.terms = suggestTerms();

    p.remoteOk = remoteOk_;
    p.needsSponsorship = needsSponsorship_;
    return p;
}

void App::saveProfileFromForm() {
    Profile p = profileFromForm();
    if (p.major.empty()) { formMessage_ = "Please choose a major."; return; }
    profile_ = p;
    profileOk_ = true;
    saveProfile(profilePath(), profile_);
    formMessage_ = "Saved. Your results have been re-ranked.";
    rerank();
    if (listings_.empty() && !fetching_) startFetch();
    requestTab_ = 1;
}

// ============================================================================
// data
// ============================================================================

void App::startFetch() {
    if (fetching_) return;
    if (fetchThread_.joinable()) fetchThread_.join();
    fetching_ = true;
    fetchDone_ = false;
    fetchResult_.clear();
    fetchReports_.clear();
    { std::lock_guard<std::mutex> lock(fetchMutex_); fetchStatus_ = "Starting..."; }

    fetchThread_ = std::thread([this] {
        SourceConfig config = loadSourceConfig(sourcesPath());
        std::vector<FetchReport> reports;
        std::vector<Listing> result = fetchAllSources(config, reports, [this](const std::string& what) {
            std::lock_guard<std::mutex> lock(fetchMutex_);
            fetchStatus_ = "Fetching " + what + "...";
        });
        std::lock_guard<std::mutex> lock(fetchMutex_);
        fetchResult_ = std::move(result);
        fetchReports_ = std::move(reports);
        fetchDone_ = true;
    });
}

void App::pollFetch() {
    if (!fetchDone_) return;
    fetchThread_.join();
    fetchDone_ = false;
    fetching_ = false;

    std::vector<Listing> result;
    std::vector<FetchReport> reports;
    {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        result.swap(fetchResult_);
        reports.swap(fetchReports_);
    }
    int okSources = 0;
    for (const auto& r : reports) if (r.error.empty()) ++okSources;

    if (result.empty()) {
        lastNotice_ = "Could not download listings. Check your internet connection.";
        return;
    }

    bool firstEver = state_.firstSeen.empty();
    std::set<std::string> fresh;
    for (const Listing& l : result)
        if (state_.isNew(l.id)) fresh.insert(l.id);

    listings_ = std::move(result);
    fetchedAt_ = std::time(nullptr);
    saveCache(ListingCache{fetchedAt_, listings_});
    for (const std::string& id : fresh) state_.firstSeen[id] = fetchedAt_;
    saveState(state_);
    if (!firstEver) newIds_.insert(fresh.begin(), fresh.end());

    rerank();

    // Count the new ones that actually match well, and notify.
    int goodNew = 0;
    const Match* top = nullptr;
    for (const Match& m : ranked_) {
        if (fresh.count(m.listing->id) && !m.termMismatch && m.score >= minScore_) {
            if (!top) top = &m;
            ++goodNew;
        }
    }
    lastNotice_ = std::to_string(listings_.size()) + " listings from " + std::to_string(okSources) + " sources";
    if (!firstEver) {
        lastNotice_ += ", " + std::to_string(goodNew) + " new match" + (goodNew == 1 ? "" : "es");
        if (goodNew > 0 && top)
            ui::notify("InternScout: " + std::to_string(goodNew) + " new internship" + (goodNew == 1 ? "" : "s"),
                       top->listing->company + " - " + top->listing->title);
    }
}

void App::rerank() {
    std::string keepId = (selected_ >= 0 && selected_ < static_cast<int>(ranked_.size())) ? ranked_[selected_].listing->id : "";
    ranked_ = profileOk_ ? rankListings(listings_, profile_) : std::vector<Match>{};
    selected_ = -1;
    for (size_t i = 0; i < ranked_.size(); ++i)
        if (ranked_[i].listing->id == keepId) selected_ = static_cast<int>(i);
    filtersDirty_ = true;
}

void App::refilter() {
    filtersDirty_ = false;
    visible_.clear();
    savedRows_.clear();
    appliedRows_.clear();
    std::string needle = text::trim(filterText_);
    for (size_t i = 0; i < ranked_.size(); ++i) {
        const Match& m = ranked_[i];
        const Listing& l = *m.listing;
        if (state_.applied.count(l.id)) appliedRows_.push_back(static_cast<int>(i));
        else if (state_.saved.count(l.id)) savedRows_.push_back(static_cast<int>(i));
        if (!allTerms_ && m.termMismatch) continue;
        if (m.score < minScore_) continue;
        if (!needle.empty() && !text::contains(l.title, needle) && !text::contains(l.company, needle) &&
            !text::contains(text::join(l.locations, " "), needle))
            continue;
        visible_.push_back(static_cast<int>(i));
    }
    // Nothing highlighted yet? Show the best match in the detail panel.
    if (selected_ < 0 && !visible_.empty()) selected_ = visible_.front();
}

void App::maybeAutoRefresh() {
    int minutes = kAutoRefreshMinutes[autoRefreshIdx_];
    if (minutes == 0 || fetching_ || !profileOk_) return;
    if (std::time(nullptr) - fetchedAt_ >= minutes * 60) {
        fetchWasAutomatic_ = true;
        startFetch();
    }
}

// ============================================================================
// drawing
// ============================================================================

void App::frame() {
    pollFetch();
    maybeAutoRefresh();
    if (filtersDirty_) refilter();

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(vp->WorkPos);
    ImGui::SetNextWindowSize(vp->WorkSize);
    ImGui::Begin("InternScout", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoBringToFrontOnFocus |
                     ImGuiWindowFlags_NoSavedSettings);

    if (ImGui::BeginTabBar("pages")) {
        auto flag = [&](int i) { return requestTab_ == i ? ImGuiTabItemFlags_SetSelected : ImGuiTabItemFlags_None; };
        if (ImGui::BeginTabItem("My profile", nullptr, flag(0))) { drawProfilePage(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Internships", nullptr, flag(1))) { drawResultsPage(); ImGui::EndTabItem(); }
        std::string savedLabel = "Saved";
        if (!savedRows_.empty()) savedLabel += " (" + std::to_string(savedRows_.size()) + ")";
        savedLabel += "###saved";
        if (ImGui::BeginTabItem(savedLabel.c_str(), nullptr, flag(2))) { drawSavedPage(); ImGui::EndTabItem(); }
        std::string appliedLabel = "Applied";
        if (!appliedRows_.empty()) appliedLabel += " (" + std::to_string(appliedRows_.size()) + ")";
        appliedLabel += "###applied";
        if (ImGui::BeginTabItem(appliedLabel.c_str(), nullptr, flag(3))) { drawAppliedPage(); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Explanation", nullptr, flag(4))) { drawScoringPage(); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }
    requestTab_ = -1;
    ImGui::End();
}

void App::drawProfilePage() {
    ImGui::BeginChild("profileScroll", ImVec2(0, 0));
    ImGui::Spacing();
    if (headingFont) ImGui::PushFont(headingFont);
    ImGui::TextUnformatted("Tell InternScout about you");
    if (headingFont) ImGui::PopFont();
    ImGui::TextColored(kMuted, "Everything here is used only to rank listings. It never leaves your Mac.");
    ImGui::Spacing();

    const float fieldWidth = 320.0f;
    ImGui::PushItemWidth(fieldWidth);

    ImGui::InputText("Name", nameBuf_, sizeof nameBuf_);

    combo("University", universityIdx_, options::universities());
    if (universityIdx_ == static_cast<int>(options::universities().size()) - 1)
        ImGui::InputText("University name", otherUniversity_, sizeof otherUniversity_);

    combo("Major", majorIdx_, options::majors());
    if (majorIdx_ == static_cast<int>(options::majors().size()) - 1)
        ImGui::InputText("Major name", otherMajor_, sizeof otherMajor_);

    combo("Degree level", degreeIdx_, options::degreeLevels());
    combo("Year of study", yearIdx_, options::yearsOfStudy());

    {
        std::vector<std::string> yearLabels;
        for (int y : options::graduationYears()) yearLabels.push_back(std::to_string(y));
        combo("Expected graduation", gradIdx_, yearLabels);
    }

    combo("Home country", countryIdx_, options::countries());
    if (countryIdx_ == static_cast<int>(options::countries().size()) - 1)
        ImGui::InputText("Country name", otherCountry_, sizeof otherCountry_);
    ImGui::PopItemWidth();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ---- interests ----
    ImGui::TextUnformatted("Interests");
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "(pick as many as you like)");
    multiSelect("interests", "Any / all fields", anyInterests_, options::interests(), customInterests_,
                selectedInterests_, 4);
    ImGui::BeginDisabled(anyInterests_);
    ImGui::PushItemWidth(240.0f);
    bool addInterest = ImGui::InputText("##newInterest", newInterest_, sizeof newInterest_,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Add interest") || addInterest) {
        std::string s = text::trim(newInterest_);
        if (!s.empty() && indexOf(customInterests_, s) < 0 && indexOf(options::interests(), s) < 0)
            customInterests_.push_back(s);
        if (!s.empty()) selectedInterests_.insert(s);
        newInterest_[0] = '\0';
    }
    ImGui::EndDisabled();

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ---- locations ----
    ImGui::TextUnformatted("Preferred locations");
    multiSelect("locations", "Anywhere", anywhere_, options::locations(), customLocations_, selectedLocations_, 4);
    ImGui::BeginDisabled(anywhere_);
    ImGui::PushItemWidth(240.0f);
    bool addLocation = ImGui::InputText("##newLocation", newLocation_, sizeof newLocation_,
                                        ImGuiInputTextFlags_EnterReturnsTrue);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    if (ImGui::Button("Add city") || addLocation) {
        std::string s = text::trim(newLocation_);
        if (!s.empty() && indexOf(customLocations_, s) < 0 && indexOf(options::locations(), s) < 0)
            customLocations_.push_back(s);
        if (!s.empty()) selectedLocations_.insert(s);
        newLocation_[0] = '\0';
    }
    ImGui::EndDisabled();
    ImGui::Checkbox("Remote internships are fine", &remoteOk_);
    ImGui::Checkbox("I would need visa sponsorship to work in the US", &needsSponsorship_);

    ImGui::Spacing(); ImGui::Separator(); ImGui::Spacing();

    // ---- terms ----
    ImGui::TextUnformatted("Which terms are you looking for?");
    if (ImGui::BeginTable("terms", 4, ImGuiTableFlags_SizingStretchSame)) {
        for (const std::string& t : options::terms()) {
            ImGui::TableNextColumn();
            bool on = selectedTerms_.count(t) > 0;
            if (ImGui::Checkbox(t.c_str(), &on)) { if (on) selectedTerms_.insert(t); else selectedTerms_.erase(t); }
        }
        ImGui::EndTable();
    }

    ImGui::Spacing(); ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, kBlue);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.22f, 0.52f, 0.92f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.38f, 0.75f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (ImGui::Button("Save and find internships", ImVec2(260, 36))) saveProfileFromForm();
    ImGui::PopStyleColor(4);
    if (!formMessage_.empty()) { ImGui::SameLine(); ImGui::TextColored(kMuted, "%s", formMessage_.c_str()); }
    ImGui::Spacing();
    ImGui::EndChild();
}

void App::drawToolbar() {
    ImGui::PushStyleColor(ImGuiCol_Button, kBlue);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    ImGui::BeginDisabled(fetching_);
    if (ImGui::Button(fetching_ ? "Refreshing..." : "Refresh now")) { fetchWasAutomatic_ = false; startFetch(); }
    ImGui::EndDisabled();
    ImGui::PopStyleColor(2);

    ImGui::SameLine();
    ImGui::PushItemWidth(150);
    if (ImGui::BeginCombo("##auto", kAutoRefreshLabels[autoRefreshIdx_])) {
        for (int i = 0; i < 5; ++i)
            if (ImGui::Selectable(kAutoRefreshLabels[i], i == autoRefreshIdx_)) autoRefreshIdx_ = i;
        ImGui::EndCombo();
    }
    ImGui::PopItemWidth();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Keep checking for new postings while the app is open.\nYou get a notification when a good new match appears.");

    ImGui::SameLine();
    ImGui::PushItemWidth(220);
    if (ImGui::InputTextWithHint("##filter", "Search company, title, city", filterText_, sizeof filterText_)) filtersDirty_ = true;
    ImGui::PopItemWidth();

    ImGui::SameLine();
    ImGui::PushItemWidth(160);
    if (ImGui::SliderInt("##min", &minScore_, 0, 100, "Min score %d")) filtersDirty_ = true;
    ImGui::PopItemWidth();

    ImGui::SameLine();
    if (ImGui::Checkbox("All terms", &allTerms_)) filtersDirty_ = true;
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Also show postings for terms you did not select.");

    // status line
    std::string status;
    if (fetching_) {
        std::lock_guard<std::mutex> lock(fetchMutex_);
        status = fetchStatus_;
    } else if (!lastNotice_.empty()) {
        status = lastNotice_;
    } else if (fetchedAt_) {
        status = std::to_string(listings_.size()) + " listings";
    }
    if (fetchedAt_) {
        long mins = (std::time(nullptr) - fetchedAt_) / 60;
        status += "  (updated " + (mins < 1 ? std::string("just now") : mins < 60 ? std::to_string(mins) + " min ago" :
                                   std::to_string(mins / 60) + " h ago") + ")";
    }
    ImGui::TextColored(kMuted, "%s", status.c_str());
}

void App::drawTable(const std::vector<int>& rows, const char* tableId) {
    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_ScrollY |
                            ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingStretchProp;
    float tableHeight = ImGui::GetContentRegionAvail().y * 0.58f;
    if (!ImGui::BeginTable(tableId, 6, flags, ImVec2(0, tableHeight))) return;
    ImGui::TableSetupScrollFreeze(0, 1);
    ImGui::TableSetupColumn("Score", ImGuiTableColumnFlags_WidthFixed, 52.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 66.0f);
    ImGui::TableSetupColumn("Company", ImGuiTableColumnFlags_WidthStretch, 1.4f);
    ImGui::TableSetupColumn("Role", ImGuiTableColumnFlags_WidthStretch, 3.2f);
    ImGui::TableSetupColumn("Location", ImGuiTableColumnFlags_WidthStretch, 1.6f);
    ImGui::TableSetupColumn("Posted", ImGuiTableColumnFlags_WidthFixed, 84.0f);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper;  // only draws the rows that are on screen (there can be thousands)
    clipper.Begin(static_cast<int>(rows.size()));
    while (clipper.Step()) {
        for (int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
            const Match& m = ranked_[rows[r]];
            const Listing& l = *m.listing;
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::PushID(rows[r]);
            bool isSelected = (rows[r] == selected_);
            ImVec4 scoreCol = m.score >= 70 ? kGreen : m.score >= 45 ? kAmber : kMuted;
            ImGui::TextColored(scoreCol, "%d", m.score);
            ImGui::SameLine(0, 0);
            // An invisible selectable spanning the row makes the whole row clickable.
            if (ImGui::Selectable("##row", isSelected, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap))
                selected_ = rows[r];
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0)) ui::openInBrowser(l.url);

            ImGui::TableSetColumnIndex(1);
            if (state_.applied.count(l.id)) ImGui::TextColored(kPurple, "APPLIED");
            else if (state_.saved.count(l.id)) ImGui::TextColored(kBlue, "SAVED");
            else if (newIds_.count(l.id)) ImGui::TextColored(kGreen, "NEW");

            ImGui::TableSetColumnIndex(2);
            ImGui::TextUnformatted(l.company.c_str());
            ImGui::TableSetColumnIndex(3);
            ImGui::TextUnformatted(l.title.c_str());
            ImGui::TableSetColumnIndex(4);
            std::string loc = l.locations.empty() ? "-" : l.locations.front();
            if (l.locations.size() > 1) loc += " +" + std::to_string(l.locations.size() - 1);
            ImGui::TextColored(kMuted, "%s", loc.c_str());
            ImGui::TableSetColumnIndex(5);
            ImGui::TextColored(kMuted, "%s", text::formatDate(l.datePosted).c_str());
            ImGui::PopID();
        }
    }
    ImGui::EndTable();
}

void App::drawDetail() {
    ImGui::BeginChild("detail", ImVec2(0, 0), ImGuiChildFlags_Borders);
    if (selected_ < 0 || selected_ >= static_cast<int>(ranked_.size())) {
        ImGui::TextColored(kMuted, "Select a row to see details. Double-click a row to open it in your browser.");
        ImGui::EndChild();
        return;
    }
    const Match& m = ranked_[selected_];
    const Listing& l = *m.listing;

    if (headingFont) ImGui::PushFont(headingFont);
    ImGui::TextWrapped("%s", l.title.c_str());
    if (headingFont) ImGui::PopFont();
    ImGui::TextUnformatted(l.company.c_str());
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "  %s", text::join(l.locations, "; ").c_str());

    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Button, kBlue);
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1, 1, 1, 1));
    if (ImGui::Button("Open & apply")) ui::openInBrowser(l.url);
    ImGui::PopStyleColor(2);
    ImGui::SameLine();
    bool saved = state_.saved.count(l.id) > 0;
    bool applied = state_.applied.count(l.id) > 0;
    if (ImGui::Button(saved ? "Unsave" : "Save")) {
        if (saved) state_.saved.erase(l.id); else state_.saved.insert(l.id);
        saveState(state_);
        filtersDirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button(applied ? "Not applied" : "Mark applied")) {
        if (applied) state_.applied.erase(l.id); else { state_.applied.insert(l.id); state_.saved.erase(l.id); }
        saveState(state_);
        filtersDirty_ = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy link")) ImGui::SetClipboardText(l.url.c_str());

    ImGui::Spacing();
    ImVec4 scoreCol = m.score >= 70 ? kGreen : m.score >= 45 ? kAmber : kMuted;
    ImGui::TextColored(scoreCol, "Score %d/100", m.score);
    ImGui::SameLine();
    ImGui::TextColored(kMuted, "  %s", text::join(m.reasons, "  |  ").c_str());
    if (!m.warnings.empty()) ImGui::TextColored(kAmber, "Watch out: %s", text::join(m.warnings, ", ").c_str());

    auto row = [](const char* k, const std::string& v) {
        if (v.empty()) return;
        ImGui::TextColored(kMuted, "%s", k);
        ImGui::SameLine(110);
        ImGui::TextWrapped("%s", v.c_str());
    };
    row("Term", text::join(l.terms, ", "));
    row("Degrees", text::join(l.degrees, ", "));
    row("Category", l.category);
    row("Sponsorship", l.sponsorship);
    row("Posted", text::formatDate(l.datePosted));
    row("Source", l.source);
    row("Link", l.url);
    if (!l.description.empty()) {
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::TextWrapped("%s", text::truncate(l.description, 3000).c_str());
    }
    ImGui::EndChild();
}

void App::drawResultsPage() {
    if (!profileOk_) {
        ImGui::Spacing();
        ImGui::TextWrapped("Fill in your profile first, then come back here.");
        if (ImGui::Button("Go to my profile")) requestTab_ = 0;
        return;
    }
    drawToolbar();
    std::string heading = "Top matches for " + (profile_.name.empty() ? "you" : profile_.name) + "  -  " +
                          text::join(profile_.terms, ", ") + "  -  " + std::to_string(visible_.size()) + " shown";
    ImGui::TextUnformatted(heading.c_str());
    if (visible_.empty() && !listings_.empty())
        ImGui::TextColored(kAmber, "Nothing scores above %d. Lower the minimum score or add more interests.", minScore_);
    drawTable(visible_, "results");
    drawDetail();
}

void App::drawSavedPage() {
    if (savedRows_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kMuted, "Nothing saved yet. Select a listing on the Internships tab and press Save.");
        return;
    }
    ImGui::Spacing();
    drawTable(savedRows_, "saved");
    drawDetail();
}

void App::drawAppliedPage() {
    if (appliedRows_.empty()) {
        ImGui::Spacing();
        ImGui::TextColored(kMuted, "Nothing here yet. Select a listing and press Mark applied once you have applied.");
        return;
    }
    ImGui::Spacing();
    drawTable(appliedRows_, "applied");
    drawDetail();
}

// ============================================================================
// scoring explanation
// ============================================================================

void App::drawScoringPage() {
    ImGui::BeginChild("scoringScroll", ImVec2(0, 0));
    ImGui::Spacing();
    if (headingFont) ImGui::PushFont(headingFont);
    ImGui::TextUnformatted("How the score is calculated");
    if (headingFont) ImGui::PopFont();
    ImGui::TextWrapped("Every listing gets a score out of 100. Six signals are added together and the total is "
                       "clamped to 0-100. The exact reasons for each listing's score are shown in its detail panel.");
    ImGui::Spacing();

    struct Row { const char* signal; const char* points; const char* how; };
    static const Row rows[] = {
        {"Term", "25 / 12 / 0",
         "25 if the posting's term matches one you picked (e.g. Summer 2027). 12 if the posting does not say. "
         "0 if it is explicitly for a term you did not pick - those are hidden unless you tick \"All terms\"."},
        {"Relevance", "up to 40",
         "Each interest expands into search words (\"Machine Learning / AI\" becomes ml, ai, deep learning...). "
         "A hit in the job title or category is worth 14, a hit only in the description is worth 6. "
         "Your major adds 6 per matching title word (up to 3) and 8 if the posting's category fits your major. "
         "A minor counts half. Capped at 40."},
        {"Location", "20 / 14 / 12 / 10 / 0",
         "20 if it is in a city you selected. 14 if it is remote and remote is fine with you. "
         "12 if you chose Anywhere. 10 if it is in your home country. Otherwise 0."},
        {"Degree level", "+10 / +6 / -15",
         "10 if the posting lists your level (Bachelor's, Master's, PhD). 6 if it does not say. "
         "-15 if it lists other levels only, e.g. PhD-only."},
        {"Freshness", "10 / 6 / 2 / 0",
         "Posted within 7 days, 30 days, 90 days, or older."},
        {"Sponsorship", "-30 / +5",
         "Only if you said you need US visa sponsorship: -30 if the posting says no sponsorship or requires "
         "citizenship, +5 if it says it sponsors."},
    };

    ImGuiTableFlags flags = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_BordersOuter |
                            ImGuiTableFlags_SizingStretchProp;
    if (ImGui::BeginTable("scoring", 3, flags)) {
        ImGui::TableSetupColumn("Signal", ImGuiTableColumnFlags_WidthFixed, 120.0f);
        ImGui::TableSetupColumn("Points", ImGuiTableColumnFlags_WidthFixed, 150.0f);
        ImGui::TableSetupColumn("How it is decided", ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableHeadersRow();
        for (const Row& r : rows) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::TextUnformatted(r.signal);
            ImGui::TableSetColumnIndex(1);
            ImGui::TextColored(kBlue, "%s", r.points);
            ImGui::TableSetColumnIndex(2);
            ImGui::TextWrapped("%s", r.how);
        }
        ImGui::EndTable();
    }

    ImGui::Spacing();
    ImGui::TextUnformatted("Small extras");
    ImGui::BulletText("+4 if the description mentions your graduation year.");
    ImGui::TextWrapped("    A \"Watch out\" warning (no point change) appears if the description asks for a rising senior or "
                       "penultimate-year student and you are in an earlier year.");

    ImGui::Spacing();
    ImGui::TextUnformatted("What a 100 looks like");
    ImGui::TextWrapped("Right term (25) + strong title match on your interests (40) + a city you chose (20) + degree fits (10) "
                       "+ posted this week (10) = 105 before clamping. That is why several listings tie at 100; ties are "
                       "ordered newest first.");

    ImGui::Spacing();
    ImGui::TextUnformatted("Reading the colours");
    ImGui::TextColored(kGreen, "70 and above");  ImGui::SameLine(); ImGui::TextUnformatted("strong match");
    ImGui::TextColored(kAmber, "45 to 69");      ImGui::SameLine(); ImGui::TextUnformatted("worth a look");
    ImGui::TextColored(kMuted, "below 45");      ImGui::SameLine(); ImGui::TextUnformatted("weak match (hidden by the default Min score of 40)");
    ImGui::Spacing();
    ImGui::EndChild();
}
