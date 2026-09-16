#include "text.hpp"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>

namespace text {

std::string lower(std::string s) {
    for (char& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return s;
}

std::string trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && std::isspace(static_cast<unsigned char>(s[start]))) ++start;
    size_t end = s.size();
    while (end > start && std::isspace(static_cast<unsigned char>(s[end - 1]))) --end;
    return s.substr(start, end - start);
}

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::string current;
    for (char c : s) {
        if (c == delim) {
            std::string piece = trim(current);
            if (!piece.empty()) out.push_back(piece);
            current.clear();
        } else {
            current += c;
        }
    }
    std::string piece = trim(current);
    if (!piece.empty()) out.push_back(piece);
    return out;
}

std::string join(const std::vector<std::string>& parts, const std::string& sep) {
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i) out += sep;
        out += parts[i];
    }
    return out;
}

bool contains(const std::string& haystack, const std::string& needle) {
    if (needle.empty()) return false;
    return lower(haystack).find(lower(needle)) != std::string::npos;
}

static bool isWordChar(char c) {
    return std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '#';
}

bool containsWord(const std::string& haystack, const std::string& word) {
    if (word.empty()) return false;
    const std::string h = lower(haystack);
    const std::string w = lower(word);
    size_t pos = h.find(w);
    while (pos != std::string::npos) {
        bool startOk = (pos == 0) || !isWordChar(h[pos - 1]);
        size_t after = pos + w.size();
        bool endOk = (after >= h.size()) || !isWordChar(h[after]);
        if (startOk && endOk) return true;
        pos = h.find(w, pos + 1);
    }
    return false;
}

std::string decodeEntities(const std::string& s) {
    // Order matters: "&amp;" must be decoded last so "&amp;lt;" -> "&lt;" not "<".
    struct Entity { const char* from; const char* to; };
    static const Entity entities[] = {
        {"&lt;", "<"}, {"&gt;", ">"}, {"&quot;", "\""}, {"&#39;", "'"}, {"&#x27;", "'"},
        {"&apos;", "'"}, {"&nbsp;", " "}, {"&#160;", " "}, {"&ndash;", "-"}, {"&mdash;", "-"},
        {"&#8211;", "-"}, {"&#8212;", "-"}, {"&rsquo;", "'"}, {"&lsquo;", "'"},
        {"&#8217;", "'"}, {"&#8216;", "'"}, {"&ldquo;", "\""}, {"&rdquo;", "\""},
        {"&#8220;", "\""}, {"&#8221;", "\""}, {"&bull;", "*"}, {"&#8226;", "*"}, {"&amp;", "&"},
    };
    std::string out = s;
    for (const Entity& e : entities) {
        size_t pos = 0;
        const size_t fromLen = std::strlen(e.from);
        while ((pos = out.find(e.from, pos)) != std::string::npos) {
            out.replace(pos, fromLen, e.to);
            pos += std::strlen(e.to);
        }
    }
    return out;
}

std::string stripHtml(const std::string& html) {
    std::string out;
    out.reserve(html.size());
    bool inTag = false;
    for (size_t i = 0; i < html.size(); ++i) {
        char c = html[i];
        if (c == '<') {
            inTag = true;
            // Block-level tags become a newline so paragraphs stay readable.
            std::string tag = lower(html.substr(i + 1, 4));
            if (tag.rfind("p", 0) == 0 || tag.rfind("br", 0) == 0 || tag.rfind("li", 0) == 0 ||
                tag.rfind("div", 0) == 0 || tag.rfind("h", 0) == 0 || tag.rfind("/p", 0) == 0 ||
                tag.rfind("/li", 0) == 0 || tag.rfind("/h", 0) == 0 || tag.rfind("ul", 0) == 0)
                out += '\n';
            continue;
        }
        if (c == '>') { inTag = false; continue; }
        if (!inTag) out += c;
    }
    out = decodeEntities(out);

    // Collapse runs of blank lines / spaces so the output is compact.
    std::string cleaned;
    int newlines = 0;
    bool lastSpace = false;
    for (char c : out) {
        if (c == '\r') continue;
        if (c == '\n') {
            if (++newlines <= 2) cleaned += '\n';
            lastSpace = true;
            continue;
        }
        if (c == ' ' || c == '\t') {
            if (!lastSpace) cleaned += ' ';
            lastSpace = true;
            continue;
        }
        newlines = 0;
        lastSpace = false;
        cleaned += c;
    }
    return trim(cleaned);
}

size_t displayWidth(const std::string& s) {
    // Count UTF-8 code points (bytes that are not continuation bytes 10xxxxxx).
    size_t n = 0;
    for (unsigned char c : s)
        if ((c & 0xC0) != 0x80) ++n;
    return n;
}

std::string truncate(const std::string& s, size_t width) {
    if (displayWidth(s) <= width) return s;
    if (width == 0) return "";
    // Walk code points until we have width-1 of them, then add an ellipsis.
    size_t count = 0, i = 0;
    while (i < s.size() && count < width - 1) {
        ++i;
        while (i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) ++i;
        ++count;
    }
    return s.substr(0, i) + "\xE2\x80\xA6";  // "…"
}

std::string pad(const std::string& s, size_t width) {
    size_t w = displayWidth(s);
    if (w >= width) return s;
    return s + std::string(width - w, ' ');
}

std::time_t parseIsoDate(const std::string& iso) {
    int y = 0, m = 0, d = 0, hh = 0, mm = 0, ss = 0;
    int n = std::sscanf(iso.c_str(), "%d-%d-%dT%d:%d:%d", &y, &m, &d, &hh, &mm, &ss);
    if (n < 3) return 0;
    std::tm tm{};
    tm.tm_year = y - 1900;
    tm.tm_mon = m - 1;
    tm.tm_mday = d;
    tm.tm_hour = hh;
    tm.tm_min = mm;
    tm.tm_sec = ss;
    return timegm(&tm);  // interpret as UTC; the timezone offset is small enough to ignore
}

std::string formatDate(std::time_t t) {
    if (t <= 0) return "unknown";
    std::tm tm{};
    localtime_r(&t, &tm);
    char buf[16];
    std::strftime(buf, sizeof buf, "%Y-%m-%d", &tm);
    return buf;
}

int daysAgo(std::time_t t) {
    if (t <= 0) return 99999;
    std::time_t now = std::time(nullptr);
    return static_cast<int>((now - t) / (60 * 60 * 24));
}

}  // namespace text
