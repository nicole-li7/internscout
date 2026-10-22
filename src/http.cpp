#include "http.hpp"

#include <curl/curl.h>

#include <mutex>

namespace {

// libcurl hands us the response in chunks; this appends each chunk to our string.
size_t writeToString(char* ptr, size_t size, size_t nmemb, void* userdata) {
    auto* out = static_cast<std::string*>(userdata);
    out->append(ptr, size * nmemb);
    return size * nmemb;
}

// curl_global_init must run exactly once per process.
void ensureCurlInitialised() {
    static std::once_flag once;
    std::call_once(once, [] { curl_global_init(CURL_GLOBAL_DEFAULT); });
}

// One curl handle per thread, kept for the thread's lifetime. Reusing it keeps
// curl's connection, DNS and TLS session caches, so the next request to the same
// host (e.g. hundreds of Greenhouse boards on boards-api.greenhouse.io) skips the
// DNS lookup and TLS handshake. A handle must never be used by two threads at once,
// hence thread_local.
struct ThreadCurl {
    CURL* handle = nullptr;
    ~ThreadCurl() { if (handle) curl_easy_cleanup(handle); }
};

CURL* threadCurl() {
    thread_local ThreadCurl curl;
    if (!curl.handle) curl.handle = curl_easy_init();
    else curl_easy_reset(curl.handle);  // forget the last request's options; the caches stay
    return curl.handle;
}

}  // namespace

static HttpResult perform(const std::string& url, const std::string* postBody, int timeoutSeconds, int stallSeconds) {
    ensureCurlInitialised();
    HttpResult result;

    CURL* curl = threadCurl();
    if (!curl) {
        result.error = "could not initialise libcurl";
        return result;
    }

    char errorBuffer[CURL_ERROR_SIZE] = {0};
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 5L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeoutSeconds));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 15L);
    if (stallSeconds > 0) {  // under 1 byte/s for that long = stuck
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 1L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, static_cast<long>(stallSeconds));
    }
    curl_easy_setopt(curl, CURLOPT_ACCEPT_ENCODING, "");  // "" = accept everything curl supports (gzip etc.)
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "InternScout/1.0 (+https://github.com/nicole-li7/internscout)");
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeToString);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &result.body);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, errorBuffer);

    struct curl_slist* headers = nullptr;
    if (postBody) {
        headers = curl_slist_append(headers, "Content-Type: application/json");
        headers = curl_slist_append(headers, "Accept: application/json");
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, postBody->c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(postBody->size()));
    }

    CURLcode code = curl_easy_perform(curl);
    if (code != CURLE_OK) {
        result.error = errorBuffer[0] ? errorBuffer : curl_easy_strerror(code);
    } else {
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &result.status);
        if (result.status < 200 || result.status >= 300)
            result.error = "HTTP " + std::to_string(result.status);
    }
    if (headers) curl_slist_free_all(headers);
    return result;
}

HttpResult httpGet(const std::string& url, int timeoutSeconds, int stallSeconds) {
    return perform(url, nullptr, timeoutSeconds, stallSeconds);
}

HttpResult httpPost(const std::string& url, const std::string& jsonBody, int timeoutSeconds, int stallSeconds) {
    return perform(url, &jsonBody, timeoutSeconds, stallSeconds);
}
