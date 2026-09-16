# InternScout

A command-line tool that searches the web for internships and ranks them for **you**.
Tell it your year, major, interests and where you want to work; it pulls thousands of
live internship postings, scores each one against your profile, and tells you what is
new since you last checked.

Written in C++17. Runs on macOS (Linux should work too).

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
| **Greenhouse** job boards | The public API of the job-board software many companies use (Stripe, Airbnb, Databricks, ...) |
| **Ashby** job boards | Same idea (Notion, OpenAI, Ramp, Linear, ...) |
| **Lever** job boards | Same idea |

Only internship / co-op titles are kept from company boards. The company list lives in
`sources.json` in the data folder (run `internscout sources` to see the path) and you can add
any company by its careers-page slug.

## Build

You need CMake and a C++17 compiler (Xcode command line tools on macOS). libcurl ships with macOS.

```sh
cmake -S . -B build
cmake --build build
cmake --install build        # puts `internscout` on your PATH (/opt/homebrew/bin)
```

Or open the folder in VS Code and press **Cmd+Shift+B**.

## Use

```sh
internscout setup            # answer a few questions (year, major, interests, locations, term)
internscout search           # fetch listings and show the best matches
internscout show 3           # full details for result 3
internscout open 3           # open result 3 in your browser to apply
internscout save 3           # bookmark it
internscout applied 3        # mark it as applied
internscout saved            # list bookmarks and applications
internscout watch            # keep checking every 30 min; macOS notification on new matches
```

Useful flags:

```
--refresh      re-download listings (otherwise a 6-hour cache is used)
--all          include weak matches and other terms
--limit N      how many results to show (default 25)
--min N        minimum score 0-100 (default 40)
--every N      minutes between checks in watch mode (default 30)
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
src/main.cpp      command-line interface
src/profile.*     profile questions + JSON save/load
src/sources.*     fetchers for each job board
src/matcher.*     scoring a listing against a profile
src/store.*       cache and seen/saved/applied state
src/http.*        libcurl wrapper
src/text.*        string helpers
src/ui.*          terminal colours, tables, notifications
third_party/      nlohmann/json (single header)
```
