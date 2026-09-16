// http.hpp - a tiny wrapper around libcurl so the rest of the app can just say
// "fetch this URL and give me the body as a string".
#pragma once

#include <string>

struct HttpResult {
    long status = 0;       // HTTP status code (200 = OK). 0 means the request never completed.
    std::string body;      // Response body (usually JSON for our sources).
    std::string error;     // Human-readable error if something went wrong.

    bool ok() const { return error.empty() && status >= 200 && status < 300; }
};

// Perform an HTTP GET. Follows redirects, asks for gzip, times out after `timeoutSeconds`.
HttpResult httpGet(const std::string& url, int timeoutSeconds = 60);

// Perform an HTTP POST with a JSON body (some job boards only offer a POST search API).
HttpResult httpPost(const std::string& url, const std::string& jsonBody, int timeoutSeconds = 60);
