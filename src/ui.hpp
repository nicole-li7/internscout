// ui.hpp - terminal colours and layout helpers.
// Colours are ANSI escape codes; they are turned off automatically when the
// output is not a terminal (e.g. piped to a file) or when NO_COLOR is set.
#pragma once

#include <string>

namespace ui {

bool coloursEnabled();

std::string bold(const std::string& s);
std::string dim(const std::string& s);
std::string green(const std::string& s);
std::string yellow(const std::string& s);
std::string red(const std::string& s);
std::string cyan(const std::string& s);
std::string magenta(const std::string& s);

// Convenience wrappers for common message kinds.
std::string ok(const std::string& s);      // green check
std::string warn(const std::string& s);    // yellow warning
std::string fail(const std::string& s);    // red cross
std::string header(const std::string& s);  // bold + underline-ish rule

// Colour a 0-100 score: green for strong matches, yellow for OK, dim for weak.
std::string scoreColour(int score, const std::string& s);

// Print a one-line status that gets overwritten by the next one (for progress).
void status(const std::string& s);
void clearStatus();

// Send a macOS notification (used by `watch`). Silently does nothing on other platforms.
void notify(const std::string& title, const std::string& body);

// How wide is the terminal? Falls back to 120 when unknown.
int terminalWidth();

}  // namespace ui
