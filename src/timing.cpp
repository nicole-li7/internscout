#include "timing.hpp"

#include <cstdio>
#include <mutex>
#include <utility>
#include <vector>

namespace timing {

namespace {

std::mutex mutex;
std::vector<std::pair<std::string, double>> latest;  // step name -> ms, in the order first seen
std::function<void(const std::string&)> printer;

// 85 ms, 1.2 s
std::string format(double ms) {
    char buf[32];
    if (ms < 1000) std::snprintf(buf, sizeof buf, "%.0f ms", ms);
    else std::snprintf(buf, sizeof buf, "%.1f s", ms / 1000);
    return buf;
}

}  // namespace

void record(const std::string& what, double ms) {
    std::lock_guard<std::mutex> lock(mutex);
    bool found = false;
    for (auto& [name, value] : latest)
        if (name == what) { value = ms; found = true; }
    if (!found) latest.emplace_back(what, ms);
    if (printer) printer(what + ": " + format(ms));
}

std::string summary() {
    std::lock_guard<std::mutex> lock(mutex);
    std::string out;
    for (const auto& [name, ms] : latest) out += (out.empty() ? "" : ", ") + name + " " + format(ms);
    return out;
}

void setPrinter(std::function<void(const std::string&)> p) {
    std::lock_guard<std::mutex> lock(mutex);
    printer = std::move(p);
}

}  // namespace timing
