#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

#include "core/models.h"

namespace lite {

struct HttpRequest {
    std::string method = "GET";
    std::string url;
    std::vector<std::pair<std::string, std::string>> headers;
    std::string body;
    RequestOptions options;
};

struct HttpCookie {
    std::string name;
    std::string value;
    std::string path;
    std::string domain;
    std::string expires;
    bool secure = false;
    bool httpOnly = false;
};

struct HttpResponse {
    int statusCode = 0;
    std::string reason;
    std::vector<std::pair<std::string, std::string>> headers;
    std::vector<HttpCookie> cookies;
    std::string body;
    double elapsedMs = 0.0;
    std::string error;
    bool canceled = false;
};

// The token is shared between the UI and the worker. Cancelling closes the
// active WinHTTP request handle, which unblocks send/receive immediately.
class CancelToken {
public:
    CancelToken() = default;
    CancelToken(const CancelToken&) = delete;
    CancelToken& operator=(const CancelToken&) = delete;

    bool cancelled() const { return cancelled_.load(std::memory_order_relaxed); }
    void cancel();
    bool attach(void* requestHandle);
    bool detach(void* requestHandle);
    void finish();

private:
    std::atomic<bool> cancelled_{false};
    std::mutex mutex_;
    void* request_ = nullptr;
    bool finished_ = false;
};

class HttpClient {
public:
    static HttpResponse execute(const HttpRequest& request, CancelToken& cancel);
};

std::string winHttpErrorText(unsigned long code);

} // namespace lite
