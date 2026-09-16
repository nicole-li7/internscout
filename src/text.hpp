// text.hpp - small string helpers used all over the app.
// Everything here is plain C++17, no third-party dependencies.
#pragma once

#include <ctime>
#include <string>
#include <vector>

namespace text {

// "Hello World" -> "hello world"
std::string lower(std::string s);

// Remove leading/trailing whitespace.
std::string trim(const std::string& s);

// Split "a, b,,c" on ',' -> {"a", "b", "c"} (each piece trimmed, empties dropped).
std::vector<std::string> split(const std::string& s, char delim);

// Join {"a","b"} with ", " -> "a, b"
std::string join(const std::vector<std::string>& parts, const std::string& sep);

// Case-insensitive "does haystack contain needle?"
bool contains(const std::string& haystack, const std::string& needle);

// Case-insensitive whole-word match ("data" matches "Data Intern" but not "database").
bool containsWord(const std::string& haystack, const std::string& word);

// Turn "<p>Hello &amp; welcome</p>" into "Hello & welcome".
std::string stripHtml(const std::string& html);

// Decode the handful of HTML entities that job boards actually use.
std::string decodeEntities(const std::string& s);

// Cut a string to `width` characters, adding "…" if it was longer.
std::string truncate(const std::string& s, size_t width);

// Pad (with spaces, on the right) so the string is exactly `width` wide.
std::string pad(const std::string& s, size_t width);

// Parse "2026-09-03T13:30:34-04:00" (or just "2026-09-03") into a Unix timestamp.
// Returns 0 if it cannot be parsed.
std::time_t parseIsoDate(const std::string& iso);

// Unix timestamp -> "2026-09-03"
std::string formatDate(std::time_t t);

// How many whole days ago was this timestamp?
int daysAgo(std::time_t t);

// Number of visible characters (UTF-8 aware enough for "…" and accents).
size_t displayWidth(const std::string& s);

}  // namespace text
