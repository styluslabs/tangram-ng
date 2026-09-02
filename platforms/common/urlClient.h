#pragma once

#include "platform.h" // UrlResponse
#include "util/asyncWorker.h"

#include <atomic>
#include <list>
#include <mutex>
#include <deque>
#include <string>
#include <thread>

namespace Tangram {

class UrlClient {

public:

    struct Options {
        uint32_t maxActiveTasks = 20;
        uint32_t connectionTimeoutMs = 3000;
        uint32_t requestTimeoutMs = 30000;
        uint32_t maxHttpVersion = 1;
        const char* userAgentString = "tangram";
    };

    UrlClient(Options options);
    ~UrlClient();

    using RequestId = uint64_t;

    RequestId addRequest(const std::string& url, const HttpOptions& _options, UrlCallback cb);

    void cancelRequest(RequestId request);
    void cancelAllRequests();

private:

    struct Request {
        std::string url;
        HttpOptions options;
        UrlCallback callback;
        RequestId id;
    };

    struct Task;

    void curlLoop();
    void curlWakeUp();

    void startPendingRequests();

    Options m_options;

    // Curl multi handle
    void *m_curlHandle = nullptr;
    void* m_curlShare = nullptr;

    bool m_curlRunning = false;

    std::unique_ptr<std::thread> m_curlWorker;
    AsyncWorker m_dispatcher = {"UrlClient dispatcher"};

    std::list<Task> m_tasks;
    uint32_t m_activeTasks = 0;

    std::deque<Request> m_requests;

    // Synchronize m_tasks and m_requests
    std::mutex m_requestMutex;

    // RequestIds
    std::atomic<uint64_t> m_requestCount{0};
};

} // namespace Tangram
