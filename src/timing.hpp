// timing.hpp - simple stopwatch logs, so we can see which step is slow.
//
//   { timing::Scope t("rank"); ranked = rankListings(...); }
//
// Each finished step is remembered (latest time per name) for the GUI status
// line, and passed to a printer if one is set (the CLI prints them).
// Safe to use from any thread.
#pragma once

#include <chrono>
#include <functional>
#include <string>

namespace timing {

// Remember that `what` took `ms` milliseconds.
void record(const std::string& what, double ms);

// "load 1.2 s, rank 85 ms, filter 2 ms" - the latest time of every step, in the order first seen.
std::string summary();

// Called with "rank: 85 ms" each time a step finishes. Empty = do not print.
void setPrinter(std::function<void(const std::string&)> printer);

// Times the block it lives in.
class Scope {
public:
    explicit Scope(std::string what) : what_(std::move(what)), start_(std::chrono::steady_clock::now()) {}
    ~Scope() { record(what_, std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start_).count()); }
    Scope(const Scope&) = delete;
    Scope& operator=(const Scope&) = delete;

private:
    std::string what_;
    std::chrono::steady_clock::time_point start_;
};

}  // namespace timing
