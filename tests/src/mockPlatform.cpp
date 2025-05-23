#include "mockPlatform.h"

#include <stdio.h>
#include <stdarg.h>
#include <iostream>
#include <fstream>
#include <string>

#ifndef TANGRAM_WINDOWS
#include <libgen.h>
#include <unistd.h>
#else
#include <direct.h> // _getcwd
#endif
#include <limits.h>

#define DEFAULT_FONT "res/fonts/NotoSans-Regular.ttf"

#include "log.h"
#include "scene/styleContext.h"

#ifdef TANGRAM_WINDOWS
#define PATH_MAX 512
#endif

//#ifdef FONTCONTEXT_STB
#define FONTSTASH_IMPLEMENTATION
#include "fontstash.h"

void TANGRAM_WakeEventLoop() {}

namespace Tangram {

class Texture;
bool userLoadSvg(const char* svg, size_t len, Texture* texture) { return false; }
FONScontext* userCreateFontstash(FONSparams* params, int atlasFontPx) { return fonsCreateInternal(params); }

NativeStyleFn userGetStyleFunction(Scene& scene, const std::string& jsSource) { return {}; }

void logStr(const std::string& msg) { fputs(msg.c_str(), stderr); }

MockPlatform::MockPlatform() {
    m_baseUrl = Url("file:///");
    char pathBuffer[PATH_MAX] = {0};
    if (getcwd(pathBuffer, PATH_MAX) != nullptr) {
        m_baseUrl = m_baseUrl.resolve(Url(std::string(pathBuffer) + "/"));
    }
}

void MockPlatform::requestRender() const {}

std::vector<FontSourceHandle> MockPlatform::systemFontFallbacksHandle() const {
    std::vector<FontSourceHandle> handles;

    handles.emplace_back(Url{DEFAULT_FONT});

    return handles;
}

bool MockPlatform::startUrlRequestImpl(const Url& _url, const HttpOptions& _options,
                                       const UrlRequestHandle _handle, UrlRequestId& _id) {

    UrlResponse response;

    if (_url == Url{DEFAULT_FONT}) {
        LOG("DEFAULT_FONT");
        response.content = getBytesFromFile(DEFAULT_FONT);
    } else if (!m_files.empty()){
        auto it = m_files.find(_url);
        if (it != m_files.end()) {
            response.content = it->second;
        } else {
            response.error = "Url contents could not be found!";
        }
    } else {

        auto allocator = [&](size_t size) {
                             response.content.resize(size);
                             return response.content.data();
                         };

        Platform::bytesFromFileSystem(_url.path().c_str(), allocator);
    }

    onUrlResponse(_handle, std::move(response));

    return false;
}

void MockPlatform::cancelUrlRequestImpl(const UrlRequestId _id) {}

void MockPlatform::putMockUrlContents(Url url, std::string contents) {
    m_files[url].assign(contents.begin(), contents.end());
}

void MockPlatform::putMockUrlContents(Url url, std::vector<char> contents) {
    m_files[url] = contents;
}

std::vector<char> MockPlatform::getBytesFromFile(const char* path) {
    std::vector<char> result;
    auto allocator = [&](size_t size) {
        result.resize(size);
        return result.data();
    };
    Platform::bytesFromFileSystem(path, allocator);
    return result;
}

void setCurrentThreadPriority(int priority) {}

void initGLExtensions() {}

} // namespace Tangram
