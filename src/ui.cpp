#include "ui.hpp"

#include <sys/ioctl.h>
#include <unistd.h>

#include <cstdlib>
#include <iostream>

namespace ui {

bool coloursEnabled() {
    static const bool enabled = [] {
        if (std::getenv("NO_COLOR")) return false;
        return isatty(STDOUT_FILENO) == 1;
    }();
    return enabled;
}

static std::string wrap(const char* code, const std::string& s) {
    if (!coloursEnabled()) return s;
    return std::string("\033[") + code + "m" + s + "\033[0m";
}

std::string bold(const std::string& s) { return wrap("1", s); }
std::string dim(const std::string& s) { return wrap("2", s); }
std::string green(const std::string& s) { return wrap("32", s); }
std::string yellow(const std::string& s) { return wrap("33", s); }
std::string red(const std::string& s) { return wrap("31", s); }
std::string cyan(const std::string& s) { return wrap("36", s); }
std::string magenta(const std::string& s) { return wrap("35", s); }

std::string ok(const std::string& s) { return green("\xE2\x9C\x94 ") + s; }      // ✔
std::string warn(const std::string& s) { return yellow("\xE2\x9A\xA0 ") + s; }   // ⚠
std::string fail(const std::string& s) { return red("\xE2\x9C\x98 ") + s; }      // ✘

std::string header(const std::string& s) {
    return bold(cyan(s)) + "\n" + dim(std::string(s.size(), '-'));
}

std::string scoreColour(int score, const std::string& s) {
    if (score >= 70) return bold(green(s));
    if (score >= 45) return yellow(s);
    return dim(s);
}

void status(const std::string& s) {
    if (!coloursEnabled()) { std::cout << s << "\n"; return; }
    std::cout << "\r\033[2K" << dim(s) << std::flush;
}

void clearStatus() {
    if (coloursEnabled()) std::cout << "\r\033[2K" << std::flush;
}

void notify(const std::string& title, const std::string& body) {
#ifdef __APPLE__
    auto escape = [](std::string s) {
        std::string out;
        for (char c : s) {
            if (c == '"' || c == '\\') out += '\\';
            out += c;
        }
        return out;
    };
    std::string script = "display notification \"" + escape(body) + "\" with title \"" + escape(title) + "\"";
    std::string cmd = "osascript -e '" + script + "' >/dev/null 2>&1";
    std::system(cmd.c_str());
#else
    (void)title; (void)body;
#endif
}

void openInBrowser(const std::string& url) {
    // Single-quote the URL for the shell, escaping any single quotes inside it.
    std::string quoted = "'";
    for (char c : url) quoted += (c == '\'') ? std::string("'\\''") : std::string(1, c);
    quoted += "'";
#ifdef __APPLE__
    std::system(("open " + quoted).c_str());
#else
    std::system(("xdg-open " + quoted).c_str());
#endif
}

int terminalWidth() {
    struct winsize w{};
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &w) == 0 && w.ws_col > 40) return w.ws_col;
    return 120;
}

}  // namespace ui
