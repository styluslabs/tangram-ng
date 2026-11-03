#include "gl/texture.h"

#include "gl/glError.h"
#include "gl/renderState.h"
#include "gl/hardware.h"
#include "log.h"
#include "map.h"
#include "platform.h"
#include "util/geom.h"
#include "util/imageLoader.h"

#include <atomic>
#include <cassert>
#include <cstring> // for memset

namespace Tangram {

static std::atomic<uint32_t> s_textureSerial{0};

Texture::Texture(TextureOptions _options, bool _disposeBuffer)
    : m_options(_options), m_disposeBuffer(_disposeBuffer), m_serial(++s_textureSerial) {}

Texture::Texture(const uint8_t* data, size_t length, TextureOptions options, bool _disposeBuffer)
    : Texture(options, _disposeBuffer) {
    loadImageFromMemory(data, length);
}

Texture::~Texture() {
    if (m_rs) {
        m_rs->queueTextureDeletion(m_glHandle);
    }
}

bool Texture::loadImageFromMemory(const uint8_t* data, size_t length) {

    int width = 0, height = 0;
    GLint internalfmt = 0;
    LOGTInit();

    m_buffer.reset(loadImage(data, length, &width, &height, &internalfmt, int(bpp())));

    if (!m_buffer) {
        // We now let raster tile task handle setting empty texture (or a proxy)
        // Default inconsistent texture data is set to a 1*1 pixel texture
        // This reduces inconsistent behavior when texture failed loading
        // texture data but a Tangram style shader requires a shader sampler
        //GLubyte pixel[4] = { 0, 0, 0, 255 };
        //setPixelData(1, 1, bpp(), pixel, bpp());
        return false;
    }

    m_options.pixelFormat = static_cast<PixelFormat>(internalfmt);
    m_bufferSize = width * height * bpp();
    resize(width, height);

    LOGT("Decoded image data: %dx%d bpp:%d", width, height, bpp());
    return true;
}

bool Texture::setPixelData(int _width, int _height, int _bytesPerPixel,
                           const GLubyte* _data, size_t _length) {

    if (!sanityCheck(_width, _height, _bytesPerPixel, _length)) {
        return false;
    }

    if (m_bufferSize != _length) {
        m_buffer.reset();
    }

    if (!m_buffer) {
        m_buffer.reset(reinterpret_cast<GLubyte*>(std::malloc(_length)));
    }

    if (!m_buffer) {
        LOGE("Could not allocate texture: Out of memory!");
        return false;
    }

    std::memcpy(m_buffer.get(), _data, _length);

    m_bufferSize = _length;

    resize(_width, _height);

    return true;
}

void Texture::setSpriteAtlas(std::unique_ptr<Tangram::SpriteAtlas> sprites) {
    m_spriteAtlas = std::move(sprites);
}

void Texture::generate(RenderState& _rs, GLuint _textureUnit) {
    GL::genTextures(1, &m_glHandle);

    _rs.texture(m_glHandle, _textureUnit, GL_TEXTURE_2D);

    GL::texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                      static_cast<GLint>(m_options.minFilter));
    GL::texParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                      static_cast<GLint>(m_options.magFilter));

    GL::texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S,
                      static_cast<GLint>(m_options.wrapS));
    GL::texParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T,
                      static_cast<GLint>(m_options.wrapT));

    m_rs = &_rs;
}

bool Texture::upload(RenderState& _rs, GLuint _textureUnit) {
    m_shouldResize = false;

    if (Hardware::maxTextureSize < m_width ||
        Hardware::maxTextureSize < m_height) {
        LOGW("Texture larger than Hardware maximum texture size");
        if (m_disposeBuffer) { m_buffer.reset(); }
        return false;
    }
    if (m_glHandle == 0) {
        generate(_rs, _textureUnit);
    } else {
        _rs.texture(m_glHandle, _textureUnit, GL_TEXTURE_2D);
    }

    auto internalfmt = static_cast<GLint>(m_options.pixelFormat);
    // desktop GL doesn't support GL_ALPHA, GLES doesn't support GL_RED, so have to use GL_R8
    GL::texImage2D(GL_TEXTURE_2D, 0, internalfmt, m_width, m_height, 0, m_options.glFormat(),
                   m_options.glType(), m_buffer.get());

    if (m_buffer && m_options.generateMipmaps) {
        GL::generateMipmap(GL_TEXTURE_2D);
    }
    return true;
}

bool Texture::bind(RenderState& _rs, GLuint _textureUnit) {

    if (!m_shouldResize) {
        if (m_glHandle == 0) { return false; }

        _rs.texture(m_glHandle, _textureUnit, GL_TEXTURE_2D);
        return true;
    }

    bool ok = upload(_rs, _textureUnit);

    if (m_disposeBuffer) { m_buffer.reset(); }

    return ok;
}

void Texture::resize(int width, int height) {
    assert(width >= 0);
    assert(height >= 0);
    m_width = width;
    m_height = height;

    if (!(Hardware::supportsTextureNPOT) &&
        !(isPowerOfTwo(m_width) && isPowerOfTwo(m_height)) &&
        (m_options.generateMipmaps || (m_options.wrapS == TextureWrap::REPEAT ||
                                       m_options.wrapT == TextureWrap::REPEAT))) {
        LOGW("OpenGL ES doesn't support texture repeat" \
             " wrapping for NPOT textures nor mipmap textures");
        LOGW("Falling back to LINEAR Filtering");
        m_options.minFilter =TextureMinFilter::LINEAR;
        m_options.magFilter = TextureMagFilter::LINEAR;
        m_options.generateMipmaps = false;
    }

    m_shouldResize = true;
}

size_t Texture::bpp() const {
    return m_options.bytesPerPixel();
}

bool Texture::sanityCheck(size_t _width, size_t _height, size_t _bytesPerPixel,
                          size_t _length) const {
    size_t dim = _width * _height;
    if (_length != dim * bpp()) {
        LOGW("Invalid data size for Texture dimension! %dx%d bpp:%d bytes:%d",
             _width, _height, _bytesPerPixel, _length);
        return false;
    }
    if (bpp() != _bytesPerPixel) {
        LOGW("PixelFormat and bytesPerPixel do not match! %d:%d",
             bpp(), _bytesPerPixel);
        return false;
    }
    return true;
}

}
