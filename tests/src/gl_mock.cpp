// Null GL driver for headless unit tests and benchmarks
// - hands out real object ids and reports success for compile/link/framebuffer status, so code paths
//  match a real driver
// - tracks GPU memory (textures, buffers, renderbuffers) and upload counts; see gl_mock.h
// - bindings are per thread (like one GL context per thread, e.g. ElevationManager's offscreen worker);
//  object sizes and stats are shared and guarded by a mutex

#include "gl.h"
#include "gl_mock.h"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstring>
#if defined(__has_include) && __has_include(<execinfo.h>)
#include <execinfo.h>
#define HAVE_BACKTRACE 1
#endif
#include <mutex>
#include <unordered_map>

#ifndef GL_TEXTURE_2D
#define GL_TEXTURE_2D 0x0DE1
#endif
#ifndef GL_RENDERBUFFER
#define GL_RENDERBUFFER 0x8D41
#endif
#ifndef GL_RG8
#define GL_RG8 0x822B
#endif
#ifndef GL_R16F
#define GL_R16F 0x822D
#endif
#ifndef GL_RGBA16F
#define GL_RGBA16F 0x881A
#endif
#ifndef GL_RGBA32F
#define GL_RGBA32F 0x8814
#endif
#ifndef GL_DEPTH24_STENCIL8
#define GL_DEPTH24_STENCIL8 0x88F0
#endif

namespace Tangram {

namespace {

struct MockState {
    std::mutex mutex;
    GLMockStats stats;
    std::atomic<GLuint> nextId{1};
    std::atomic<GLint> nextUniform{1};
    std::atomic<uint64_t> drawCalls{0};
    std::unordered_map<GLuint, size_t> texBytes;
    std::unordered_map<GLuint, size_t> bufBytes;
    std::unordered_map<GLuint, size_t> rbBytes;
};

// per-thread "context" state
struct Bindings {
    GLenum activeUnit = 0;  // texture unit index (activeTexture(GL_TEXTURE0 + n))
    std::unordered_map<GLenum, GLuint> boundTex;  // key: unit << 16 | target
    std::unordered_map<GLenum, GLuint> boundBuffer;  // key: target
    GLuint boundRenderbuffer = 0;
};

MockState& state() {
    static MockState s;
    return s;
}

Bindings& bindings() {
    static thread_local Bindings b;
    return b;
}

using Lock = std::lock_guard<std::mutex>;

GLuint genId() { return state().nextId++; }

size_t bytesPerPixel(GLint internalFormat, GLenum format) {
    switch (internalFormat) {
    case GL_R8: case GL_ALPHA: case GL_LUMINANCE: case GL_RED: return 1;
    case GL_RG8: case GL_R16F: case GL_DEPTH_COMPONENT16: return 2;
    case GL_RGB8: case GL_RGB: return 3;
    case GL_RGBA8: case GL_RGBA: case GL_R32F: case GL_R32UI:
    case GL_DEPTH_COMPONENT24: case GL_DEPTH24_STENCIL8: return 4;
    case GL_RGBA16F: return 8;
    case GL_RGBA32F: return 16;
    default: break;
    }
    return format == GL_RGB ? 3 : 4;
}

void updatePeaks() {
    auto& s = state().stats;
    s.textureBytesPeak = std::max(s.textureBytesPeak, s.textureBytes);
    s.bufferBytesPeak = std::max(s.bufferBytesPeak, s.bufferBytes);
}

// set size of object `id` in `sizes`, adjusting `total`
void setSize(std::unordered_map<GLuint, size_t>& sizes, size_t& total, GLuint id, size_t bytes) {
    if (id == 0) { return; }
    size_t& cur = sizes[id];
    total = total - cur + bytes;
    cur = bytes;
    updatePeaks();
}

void deleteIds(std::unordered_map<GLuint, size_t>& sizes, size_t& total, size_t* count,
               GLsizei n, const GLuint* ids) {
    for (GLsizei i = 0; i < n; ++i) {
        auto it = sizes.find(ids[i]);
        if (it == sizes.end()) { continue; }
        total -= it->second;
        sizes.erase(it);
        if (count && *count > 0) { --*count; }
    }
}

GLuint boundTexture(GLenum target) {
    auto& b = bindings();
    auto it = b.boundTex.find((b.activeUnit << 16) | target);
    return it != b.boundTex.end() ? it->second : 0;
}

GLuint boundBuffer(GLenum target) {
    auto& b = bindings();
    auto it = b.boundBuffer.find(target);
    return it != b.boundBuffer.end() ? it->second : 0;
}

}  // namespace

const GLMockStats& glMockStats() {
    auto& st = state();
    Lock lock(st.mutex);
    st.stats.drawCalls = st.drawCalls;
    return st.stats;
}

void glMockResetPeaks() {
    Lock lock(state().mutex);
    auto& s = state().stats;
    s.textureBytesPeak = s.textureBytes;
    s.bufferBytesPeak = s.bufferBytes;
}

GLenum GL::getError() {
    return 0;
}

const GLubyte* GL::getString(GLenum name) {
    if (name == GL_VERSION) { return reinterpret_cast<const GLubyte*>("3.3 gl_mock"); }
    if (name == GL_EXTENSIONS) { return reinterpret_cast<const GLubyte*>(""); }
    return nullptr;
}

void GL::clear(GLbitfield mask) {
}
void GL::lineWidth(GLfloat width) {
}
void GL::viewport(GLint x, GLint y, GLsizei width, GLsizei height) {
}

void GL::enable(GLenum id) {
}
void GL::disable(GLenum id) {
}
void GL::depthFunc(GLenum func) {
}
void GL::depthMask(GLboolean flag) {
}
void GL::depthRange(GLfloat n, GLfloat f) {
}
void GL::clearDepth(GLfloat d) {
}
void GL::blendFunc(GLenum sfactor, GLenum dfactor) {
}
void GL::stencilFunc(GLenum func, GLint ref, GLuint mask) {
}
void GL::stencilMask(GLuint mask) {
}
void GL::stencilOp(GLenum fail, GLenum zfail, GLenum zpass) {
}
void GL::clearStencil(GLint s) {
}
void GL::colorMask(GLboolean red, GLboolean green, GLboolean blue, GLboolean alpha) {
}
void GL::cullFace(GLenum mode) {
}
void GL::frontFace(GLenum mode) {
}
void GL::clearColor(GLclampf red, GLclampf green, GLclampf blue, GLclampf alpha) {
}
void GL::getIntegerv(GLenum pname, GLint *params ) {
    switch (pname) {
    case GL_MAX_TEXTURE_SIZE: *params = 16384; break;
    case GL_MAX_COMBINED_TEXTURE_IMAGE_UNITS: *params = 32; break;
    case GL_DEPTH_BITS: *params = 24; break;
    default: *params = 0; break;  // includes GL_FRAMEBUFFER_BINDING (default framebuffer)
    }
}

// Program
void GL::useProgram(GLuint program) {
}
void GL::deleteProgram(GLuint program) {
}
void GL::deleteShader(GLuint shader) {
}
GLuint GL::createShader(GLenum type) {
    return genId();
}
GLuint GL::createProgram() {
    return genId();
}

void GL::compileShader(GLuint shader) {
}
void GL::attachShader(GLuint program, GLuint shader) {
}
void GL::linkProgram(GLuint program) {
}

void GL::shaderSource(GLuint shader, GLsizei count, const GLchar **string, const GLint *length) {
}
void GL::getShaderInfoLog(GLuint shader, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
    if (length) { *length = 0; }
    if (infoLog && bufSize > 0) { infoLog[0] = '\0'; }
}
void GL::getProgramInfoLog(GLuint program, GLsizei bufSize, GLsizei *length, GLchar *infoLog) {
    if (length) { *length = 0; }
    if (infoLog && bufSize > 0) { infoLog[0] = '\0'; }
}
GLint GL::getUniformLocation(GLuint program, const GLchar *name) {
    // ShaderProgram caches locations, so a unique location per call is enough
    return state().nextUniform++;
}
GLint GL::getAttribLocation(GLuint program, const GLchar *name) {
    return 0;
}
void GL::getProgramiv(GLuint program, GLenum pname, GLint *params) {
    *params = (pname == GL_LINK_STATUS) ? GL_TRUE : 0;
}
void GL::getShaderiv(GLuint shader, GLenum pname, GLint *params) {
    *params = (pname == GL_COMPILE_STATUS) ? GL_TRUE : 0;
}

// Buffers
void GL::bindBuffer(GLenum target, GLuint buffer) {
    bindings().boundBuffer[target] = buffer;
}
void GL::deleteBuffers(GLsizei n, const GLuint *buffers) {
    auto& st = state();
    Lock lock(st.mutex);
    deleteIds(st.bufBytes, st.stats.bufferBytes, &st.stats.buffers, n, buffers);
    for (auto& kv : bindings().boundBuffer) {
        if (std::find(buffers, buffers + n, kv.second) != buffers + n) { kv.second = 0; }
    }
}
void GL::genBuffers(GLsizei n, GLuint *buffers) {
    auto& st = state();
    Lock lock(st.mutex);
    for (GLsizei i = 0; i < n; ++i) {
        buffers[i] = genId();
        st.bufBytes[buffers[i]] = 0;
        ++st.stats.buffers;
    }
}
void GL::bufferData(GLenum target, GLsizeiptr size, const void *data, GLenum usage) {
    auto& st = state();
    Lock lock(st.mutex);
    setSize(st.bufBytes, st.stats.bufferBytes, boundBuffer(target), size_t(size));
    ++st.stats.bufferUploads;
    st.stats.bufferUploadBytes += size;
}
void GL::bufferSubData(GLenum target, GLintptr offset, GLsizeiptr size, const void *data) {
    auto& st = state();
    Lock lock(st.mutex);
    ++st.stats.bufferUploads;
    st.stats.bufferUploadBytes += size;
}
void GL::readPixels(GLint x, GLint y, GLsizei width, GLsizei height,
                    GLenum format, GLenum type, GLvoid* pixels) {
    // all formats read by tangram (RGBA/UNSIGNED_BYTE, RED_INTEGER/UNSIGNED_INT) are 4 bytes per pixel
    if (pixels && width > 0 && height > 0) { memset(pixels, 0, size_t(width)*height*4); }
}

// Texture
void GL::bindTexture(GLenum target, GLuint texture ) {
    auto& b = bindings();
    b.boundTex[(b.activeUnit << 16) | target] = texture;
}
void GL::activeTexture(GLenum texture) {
    bindings().activeUnit = texture - GL_TEXTURE0;
}
void GL::genTextures(GLsizei n, GLuint *textures ) {
    auto& st = state();
    Lock lock(st.mutex);
    for (GLsizei i = 0; i < n; ++i) {
        textures[i] = genId();
        st.texBytes[textures[i]] = 0;
        ++st.stats.textures;
    }
}
void GL::deleteTextures(GLsizei n, const GLuint *textures) {
    auto& st = state();
    Lock lock(st.mutex);
    deleteIds(st.texBytes, st.stats.textureBytes, &st.stats.textures, n, textures);
    for (auto& kv : bindings().boundTex) {
        if (std::find(textures, textures + n, kv.second) != textures + n) { kv.second = 0; }
    }
}
void GL::texParameteri(GLenum target, GLenum pname, GLint param ) {
}
// BENCH_DUMP_TEX=FILE: log every texture upload with its size and caller (for finding nondeterminism)
static void dumpTexUpload(const char* fn, GLuint tex, GLsizei w, GLsizei h, size_t bytes) {
    static FILE* f = getenv("BENCH_DUMP_TEX") ? fopen(getenv("BENCH_DUMP_TEX"), "w") : nullptr;
    if (!f) { return; }
    fprintf(f, "%s tex%u %dx%d %zu", fn, tex, w, h, bytes);
#ifdef HAVE_BACKTRACE
    void* frames[6];
    int n = backtrace(frames, 6);
    char** syms = backtrace_symbols(frames, n);
    for (int i = 2; i < n; ++i) {
        const char* sym = strchr(syms[i], '(');
        fprintf(f, " <%.*s", sym ? int(strcspn(sym + 1, "+)")) : 0, sym ? sym + 1 : "");
    }
    free(syms);
#endif
    fputc('\n', f);
}

void GL::texImage2D(GLenum target, GLint level, GLint internalFormat, GLsizei width, GLsizei height,
                    GLint border, GLenum format, GLenum type, const GLvoid *pixels) {
    auto& st = state();
    Lock lock(st.mutex);
    size_t bytes = size_t(width)*height*bytesPerPixel(internalFormat, format);
    if (level == 0) { setSize(st.texBytes, st.stats.textureBytes, boundTexture(target), bytes); }
    ++st.stats.textureUploads;
    if (pixels) { st.stats.textureUploadBytes += bytes; }
    if (pixels) { dumpTexUpload("texImage2D", boundTexture(target), width, height, bytes); }
}
void GL::texSubImage2D(GLenum target, GLint level, GLint xoffset, GLint yoffset, GLsizei width, GLsizei height,
                       GLenum format, GLenum type, const GLvoid *pixels) {
    auto& st = state();
    Lock lock(st.mutex);
    ++st.stats.textureUploads;
    st.stats.textureUploadBytes += size_t(width)*height*bytesPerPixel(0, format);
    dumpTexUpload("texSubImage2D", boundTexture(target), width, height, size_t(width)*height*bytesPerPixel(0, format));
}
void GL::generateMipmap(GLenum target) {
    // full mip chain adds 1/3 to level 0 size
    auto& st = state();
    Lock lock(st.mutex);
    GLuint tex = boundTexture(target);
    auto it = st.texBytes.find(tex);
    if (it != st.texBytes.end()) {
        setSize(st.texBytes, st.stats.textureBytes, tex, it->second + it->second/3);
    }
}

void GL::enableVertexAttribArray(GLuint index) {
}
void GL::disableVertexAttribArray(GLuint index) {
}
void GL::vertexAttribPointer(GLuint index, GLint size, GLenum type, GLboolean normalized,
                             GLsizei stride, const void *pointer) {
}

void GL::bindAttribLocation(GLuint program, GLuint index, const GLchar *name) {}

void GL::drawArrays(GLenum mode, GLint first, GLsizei count ) {
    ++state().drawCalls;
}
void GL::drawElements(GLenum mode, GLsizei count, GLenum type, const GLvoid *indices ) {
    ++state().drawCalls;
}

void GL::uniform1f(GLint location, GLfloat v0) {
}
void GL::uniform2f(GLint location, GLfloat v0, GLfloat v1) {
}
void GL::uniform3f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2) {
}
void GL::uniform4f(GLint location, GLfloat v0, GLfloat v1, GLfloat v2, GLfloat v3) {
}

void GL::uniform1i(GLint location, GLint v0) {
}
void GL::uniform2i(GLint location, GLint v0, GLint v1) {
}
void GL::uniform3i(GLint location, GLint v0, GLint v1, GLint v2) {
}
void GL::uniform4i(GLint location, GLint v0, GLint v1, GLint v2, GLint v3) {
}

void GL::uniform1fv(GLint location, GLsizei count, const GLfloat *value) {
}
void GL::uniform2fv(GLint location, GLsizei count, const GLfloat *value) {
}
void GL::uniform3fv(GLint location, GLsizei count, const GLfloat *value) {
}
void GL::uniform4fv(GLint location, GLsizei count, const GLfloat *value) {
}
void GL::uniform1iv(GLint location, GLsizei count, const GLint *value) {
}
void GL::uniform2iv(GLint location, GLsizei count, const GLint *value) {
}
void GL::uniform3iv(GLint location, GLsizei count, const GLint *value) {
}
void GL::uniform4iv(GLint location, GLsizei count, const GLint *value) {
}

void GL::uniformMatrix2fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
}
void GL::uniformMatrix3fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
}
void GL::uniformMatrix4fv(GLint location, GLsizei count, GLboolean transpose, const GLfloat *value) {
}

// mapbuffer (Hardware::supportsMapBuffer stays false, so these aren't used)
void* GL::mapBuffer(GLenum target, GLenum access) {
    return nullptr;
}
GLboolean GL::unmapBuffer(GLenum target) {
    return true;
}

void GL::finish(void) {
}

// VAO
void GL::bindVertexArray(GLuint array) {
}
void GL::deleteVertexArrays(GLsizei n, const GLuint *arrays) {
}
void GL::genVertexArrays(GLsizei n, GLuint *arrays) {
    for (GLsizei i = 0; i < n; ++i) { arrays[i] = genId(); }
}

// Framebuffer
void GL::bindFramebuffer(GLenum target, GLuint framebuffer) {
}
void GL::genFramebuffers(GLsizei n, GLuint *framebuffers) {
    for (GLsizei i = 0; i < n; ++i) { framebuffers[i] = genId(); }
}
void GL::framebufferTexture2D(GLenum target, GLenum attachment, GLenum textarget,
                              GLuint texture, GLint level) {
}
void GL::renderbufferStorage(GLenum target, GLenum internalformat, GLsizei width,
                             GLsizei height) {
    auto& st = state();
    Lock lock(st.mutex);
    setSize(st.rbBytes, st.stats.renderbufferBytes, bindings().boundRenderbuffer,
            size_t(width)*height*bytesPerPixel(internalformat, 0));
}
void GL::framebufferRenderbuffer(GLenum target, GLenum attachment,
                                 GLenum renderbuffertarget, GLuint renderbuffer) {
}
void GL::genRenderbuffers(GLsizei n, GLuint *renderbuffers) {
    for (GLsizei i = 0; i < n; ++i) { renderbuffers[i] = genId(); }
}
void GL::bindRenderbuffer(GLenum target, GLuint renderbuffer) {
    bindings().boundRenderbuffer = renderbuffer;
}
void GL::deleteFramebuffers(GLsizei n, const GLuint *framebuffers) {
}
void GL::deleteRenderbuffers(GLsizei n, const GLuint *renderbuffers) {
    auto& st = state();
    Lock lock(st.mutex);
    deleteIds(st.rbBytes, st.stats.renderbufferBytes, nullptr, n, renderbuffers);
}
GLenum GL::checkFramebufferStatus(GLenum target) {
    return GL_FRAMEBUFFER_COMPLETE;
}

}
