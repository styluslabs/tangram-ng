#include "platform.h"
#include "log.h"
#include "debug/textDisplay.h"

#include <fstream>
#include <string>
#include <cassert>

#ifdef LOGTIME
std::chrono::time_point<std::chrono::system_clock> tangram_log_time_start, tangram_log_time_last;
std::mutex tangram_log_time_mutex;
#endif

// define this as an empty fn and override requestRender() and notifyRender() if different behavior needed
extern void TANGRAM_WakeEventLoop();

namespace Tangram {

char const* Platform::shutdown_message = "Shutting down";
char const* Platform::cancel_message = "Request canceled";
char const* Platform::offline_message = "Offline";

Platform::Platform() : m_continuousRendering(false) {}

Platform::~Platform() {}

void Platform::requestRender() const {
    if (!m_shutdown && !m_renderRequested.exchange(true)) {
        TANGRAM_WakeEventLoop();
    }
}

bool Platform::notifyRender() const {
    return m_renderRequested.exchange(false);
}

void Platform::setContinuousRendering(bool _isContinuous) {
    m_continuousRendering = _isContinuous;
}

bool Platform::isContinuousRendering() const {
    return m_continuousRendering;
}

bool Platform::bytesFromFileSystem(const char* _path, std::function<char*(size_t)> _allocator) {
    std::ifstream resource(_path, std::ifstream::ate | std::ifstream::binary);

    if(!resource.is_open()) {
        LOGW("Failed to read file at path: %s", _path);
        return false;
    }

    size_t size = resource.tellg();
    char* cdata = _allocator(size);

    resource.seekg(std::ifstream::beg);
    resource.read(cdata, size);
    resource.close();

    return true;
}

FontSourceHandle Platform::systemFont(const std::string& _name, const std::string& _weight, const std::string& _face) const {
    // No-op by default
    return FontSourceHandle();
}

std::vector<FontSourceHandle> Platform::systemFontFallbacksHandle() const {
    // No-op by default
    return {};
}

void Platform::shutdown() {
    if (m_shutdown.exchange(true)) { return; }

    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);

        for (auto& entry : m_urlCallbacks) {
            auto& request = entry.second;
            if (request.callback) {
                UrlResponse response;
                response.error = shutdown_message;
                request.callback(std::move(response));
            }

            if (request.cancelable) {
                cancelUrlRequestImpl(request.id);
            }
        }
        m_urlCallbacks.clear();
    }
}

UrlRequestHandle Platform::startUrlRequest(Url _url, UrlCallback&& _callback) {
    HttpOptions opts;
    if (!defaultUserAgent.empty()) { opts.headers = "User-Agent: " + defaultUserAgent + "\r\n"; }
    return startUrlRequest(_url, opts, std::move(_callback));
}

UrlRequestHandle Platform::startUrlRequest(Url _url, const HttpOptions& _options, UrlCallback&& _callback) {

    assert(_callback);

    if (m_shutdown || (isOffline && !_url.hasFileScheme())) {
        UrlResponse response;
        response.error = m_shutdown ? shutdown_message : offline_message;
        _callback(std::move(response));
        return 0;
    }

    UrlRequestHandle handle = ++m_urlRequestCount;

    // Need to do this in advance in case startUrlRequest calls back synchronously.
    UrlRequestEntry* entry = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        auto it = m_urlCallbacks.emplace(handle, UrlRequestEntry{std::move(_callback), 0, false});
        entry = &it.first->second;
    }

    // Start Platform specific url request
    if (startUrlRequestImpl(_url, _options, handle, entry->id)) {
        entry->cancelable = true;
    }

    return handle;
}

void Platform::cancelUrlRequest(const UrlRequestHandle _request) {
    if (_request == 0) { return; }

    // -1 cancels all requests
    if (_request == UrlRequestHandle(-1)) {
        cancelUrlRequestImpl(UrlRequestId(-1));
        {
            std::lock_guard<std::mutex> lock(m_callbackMutex);
            m_urlCallbacks.clear();
        }
        return;
    }

    UrlRequestId id = 0;
    UrlCallback callback;
    bool cancelable = false;

    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        auto it = m_urlCallbacks.find(_request);
        if (it != m_urlCallbacks.end()) {
            id = it->second.id;
            cancelable = it->second.cancelable;
            //if (!cancelable) {
            callback = std::move(it->second.callback);
            m_urlCallbacks.erase(it);
        }
    }

    if (cancelable) {
        cancelUrlRequestImpl(id);
    }
    // We now always remove and run callback immediately
    if (callback) {
        UrlResponse response;
        response.error = cancel_message;
        callback(std::move(response));
    }
}

void Platform::onUrlResponse(const UrlRequestHandle _request, UrlResponse&& _response) {
    if (m_shutdown) {
        LOGW("onUrlResponse after shutdown");
        return;
    }
    bytesDownloaded += _response.content.size();
    // Find the callback associated with the request.
    UrlCallback callback;
    {
        std::lock_guard<std::mutex> lock(m_callbackMutex);
        auto it = m_urlCallbacks.find(_request);
        if (it != m_urlCallbacks.end()) {
            callback = std::move(it->second.callback);
            m_urlCallbacks.erase(it);
        }
    }
    if (callback) { callback(std::move(_response)); }
}

void logAll(const std::string& msg) {
    logStr(msg);  // to avoid problems with '%' in msg
    TextDisplay::Instance().log(msg);
}

} // namespace Tangram
