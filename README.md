# InternScout

A Mac app that searches the web for internships and ranks them for **you**.
Pick your university, degree level, year, major, interests and where you want to work;
it pulls thousands of live internship postings, scores each one against your profile,
and tells you what is new since you last checked.

Written in C++17 with SDL2 + Dear ImGui for the window. A command-line version
(`internscout`) is built alongside it and shares the same profile and data.

## The app

- **My profile** - dropdowns for university, major, degree level, year of study and
  graduation year; checkbox lists for interests, preferred locations and terms, each
  with an "Any" option; add your own interest or city if it is not listed.
- **Internships** - every matching posting ranked by score, with a NEW badge for ones
  that appeared since you last looked. Click a row for details and the reasons behind
  its score; double-click (or press *Open & apply*) to open it in your browser.
  Filter by text, minimum score, internships vs co-ops, or include other terms.
- **Saved** and **Applied** - bookmarks and the applications you have marked.
- **Explanation** - how the match score is calculated.
- **Auto-refresh** - leave it open and it re-checks every 15-120 minutes and sends a
  macOS notification when a good new match appears.

## The command line

```
$ internscout search

Top internships for Nicole (Summer 2027)
----------------------------------------
3 new since your last check

1.  100  NEW      Apera AI     Machine Learning Applied Scientist Co-op   Vancouver, BC, Canada  2026-09-15
         Summer 2027 | matches: machine learning | Vancouver, BC, Canada | posted this week
2.  100  SAVED    Robinhood    iOS Software Developer Intern              Toronto, ON, Canada    2026-09-14
         Summer 2027 | matches: software | Toronto, ON, Canada | posted this week
...
```

## Where the listings come from

| Source | What it is |
|---|---|
| **Simplify** internship list | A community-maintained list of tech internships on GitHub (~4,000 active postings, updated daily) |
| **Community list** | A second, smaller GitHub internship list |
| **Company job boards** | Direct queries to the public APIs of the software companies host their careers pages on: Greenhouse, Ashby, Lever, Workday, SmartRecruiters and Workable |

With **Deep search** on (the default), InternScout reads every link in the internship
lists, works out which company job boards they point at (about 2,400 boards) and queries
each one directly. That picks up every internship those companies have posted, including
small companies and roles nobody submitted to the lists: roughly 13,000 postings from
1,700 companies instead of 4,000 from 800. A deep refresh takes a minute or two and runs
in the background; turn it off in the toolbar for a quick refresh of just the lists.

Only internship / co-op titles are kept from company boards. Extra boards can be added by
hand in `sources.json` in the data folder (run `internscout sources` to see the path).

Anything you save or mark as applied is copied into your own data, so it stays on the
Saved / Applied pages even after the posting closes (it is then shown as closed).

## Build

You need CMake, a C++17 compiler (Xcode command line tools) and SDL2 (`brew install sdl2`).
libcurl and OpenGL ship with macOS.

```sh
cmake -S . -B build
cmake --build build
cmake --install build        # InternScout.app -> /Applications, `internscout` -> /opt/homebrew/bin
```

Or open the folder in VS Code and press **Cmd+Shift+B**. The built app is at
`build/InternScout.app` and is self-contained (SDL is copied into the bundle).

## Command-line use

```sh
internscout setup            # answer a few questions (year, major, interests, locations, term)
internscout search           # fetch listings and show the best matches
internscout show 3           # full details for result 3
internscout open 3           # open result 3 in your browser to apply
internscout save 3           # bookmark it
internscout applied 3        # mark it as applied
internscout saved            # list bookmarks and applications
internscout watch            # keep checking every 30 min; macOS notification on new matches
internscout sources          # what is being searched; `sources deep off` for quick mode
```

Useful flags:

```
--refresh      re-download listings (otherwise a 6-hour cache is used)
--all          include weak matches and other terms
--limit N      how many results to show (default 25)
--min N        minimum score 0-100 (default 40)
--every N      minutes between checks in watch mode (default 30)
--type X       intern or coop: show only internships, or only co-ops
```

## How matching works

Each listing gets a score out of 100:

| Signal | Points |
|---|---|
| Term matches one you asked for (e.g. Summer 2027) | up to 25 |
| Title / category / description match your interests and major | up to 40 |
| Location is one you listed, remote, or in your country | up to 20 |
| Degree level fits (Bachelor's / Master's / PhD) | up to 10, or a penalty |
| Posted recently | up to 10 |
| Requires citizenship / no sponsorship when you need it | -30 |

Listings explicitly for a different term are hidden unless you pass `--all`.
The reasons for every score are printed under each result, so nothing is a black box.

## Your data

Everything is stored locally in `~/Library/Application Support/InternScout/`:

- `profile.json` - your answers from `setup`
- `sources.json` - which job boards to check
- `listings.json` - cached postings from the last fetch
- `state.json` - what you have seen, saved and applied to

Nothing about you is sent anywhere; the app only downloads public job listings.

## Project layout

```
src/gui/main_gui.cpp  window, OpenGL and event loop (SDL2 + Dear ImGui)
src/gui/app.*         the app: profile form, results table, detail panel, background refresh
src/options.*         dropdown / checkbox choices and how they expand into search words
src/main.cpp      command-line interface
src/profile.*     profile questions + JSON save/load
src/sources.*     fetchers for each job board
src/matcher.*     scoring a listing against a profile
src/store.*       cache and seen/saved/applied state
src/http.*        libcurl wrapper
src/text.*        string helpers
src/ui.*          terminal colours, tables, notifications
third_party/      nlohmann/json (single header), Dear ImGui
tools/            app icon generator, bundle script
```
