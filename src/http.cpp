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

}  // namespace

static HttpResult perform(const std::string& url, const std::string* postBody, int timeoutSeconds) {
    ensureCurlInitialised();
    HttpResult result;

    CURL* curl = curl_easy_init();
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
    curl_easy_cleanup(curl);
    return result;
}

HttpResult httpGet(const std::string& url, int timeoutSeconds) { return perform(url, nullptr, timeoutSeconds); }

HttpResult httpPost(const std::string& url, const std::string& jsonBody, int timeoutSeconds) {
    return perform(url, &jsonBody, timeoutSeconds);
}
