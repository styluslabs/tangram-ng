#include "imageLoader.h"
#include "log.h"
#include <memory>

#ifndef TANGRAM_NO_STB_IMPL
// Enable only JPEG, PNG, GIF, TGA and PSD
#define STBI_NO_BMP
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM

#define STB_IMAGE_IMPLEMENTATION
#endif
#include "stb_image.h"

#define TANGRAM_TIFF_SUPPORT
#define TANGRAM_LERC_SUPPORT
#define TANGRAM_WEBP_SUPPORT

#ifdef TANGRAM_TIFF_SUPPORT
#define TINY_DNG_LOADER_IMPLEMENTATION
#define TINY_DNG_LOADER_ENABLE_ZIP
#define TINY_DNG_NO_EXCEPTION
#define TINY_DNG_LOADER_NO_STB_IMAGE_INCLUDE  // this is simpler than #undef STB_IMAGE_IMPLEMENTATION
#define TINY_DNG_PPRINTF LOG  // for profiling (if TINY_DNG_LOADER_PROFILING defined at compile time)
//#ifdef TANGRAM_RELWITHDEBINFO #define TINY_DNG_LOADER_PROFILING #endif
#include "tinydng/tiny_dng_loader.h"
#endif

#ifdef TANGRAM_LERC_SUPPORT
#include "Lerc.h"
#endif

#ifdef TANGRAM_WEBP_SUPPORT
#include "webp/decode.h"
#endif

namespace Tangram {

static uint8_t* flipImage(const uint8_t* data, int width, int height, int bpp) {
    // need to flip image vertically for OpenGL coordinate system
    // stbi_set_flip_vertically_on_load flips image in place, requiring 3x memcpy per row; we'd also need to
    //  switch to stbi_set_flip_vertically_on_load_thread to avoid conflict w/ other users of stb_image
    uint8_t* flipped = reinterpret_cast<uint8_t*>(std::malloc(width*height*bpp));
    int rowSize = width*bpp;
    for (int y = 0; y < height; ++y) {
        const uint8_t* src = &data[y*rowSize];
        uint8_t* dst = &flipped[(height - y - 1)*rowSize];
        std::memcpy(dst, src, rowSize);
    }
    return flipped;
}

struct malloc_deleter { void operator()(void* x) { std::free(x); } };

uint8_t* loadImage(const uint8_t* data, size_t length, int* width, int* height, GLint* pixelfmt, int channels) {
    if (length < 2) return nullptr;
    // check for TIFF
    if ((data[0] == 'I' && data[1] == 'I') || (data[0] == 'M' && data[1] == 'M')) {
#ifdef TANGRAM_TIFF_SUPPORT
        std::string warn, err;
        std::vector<tinydng::DNGImage> images;
        std::vector<tinydng::FieldInfo> custom_fields;

        bool ret = tinydng::LoadDNGFromMemory((const char*)data, length, custom_fields, &images, &warn, &err);
        if (!ret || images.empty() || images[0].data.empty()) {
            LOGE("Error loading TIFF: %s", err.c_str());
            return nullptr;
        }

        auto& image = images[0];
        GLint fmt = 0;
        if (image.sample_format == tinydng::SAMPLEFORMAT_IEEEFP) {
            if (image.samples_per_pixel == 1 && image.bits_per_sample == 32) {
                fmt = GL_R32F;
            }
        } else if (image.sample_format != tinydng::SAMPLEFORMAT_INT &&
                   image.sample_format != tinydng::SAMPLEFORMAT_UINT) {
          // not supported
        } else if (image.bits_per_sample == 8) {
            if (image.samples_per_pixel == 1) fmt = GL_R8;
            else if (image.samples_per_pixel == 3) fmt = GL_RGB8;
            else if (image.samples_per_pixel == 4) fmt = GL_RGBA8;
        } else if (image.samples_per_pixel == 1) {
            // convert int16 and int32 images to float
            std::vector<float> fdata(image.width*image.height);
            if (image.bits_per_sample == 16) {
                int16_t* src = (int16_t*)image.data.data();
                for (size_t ii = 0; ii < fdata.size(); ++ii) { fdata[ii] = float(src[ii]); }
            } else if (image.bits_per_sample == 32) {
                int32_t* src = (int32_t*)image.data.data();
                for (size_t ii = 0; ii < fdata.size(); ++ii) { fdata[ii] = float(src[ii]); }
            }
            *width = image.width;
            *height = image.height;
            *pixelfmt = GL_R32F;
            return flipImage((uint8_t*)fdata.data(), image.width, image.height, 4);
        }
        if (!fmt) {
            LOGE("Unsupported TIFF: %d bits per sample, %d samples per pixel",
                 image.bits_per_sample, image.samples_per_pixel);
            return nullptr;
        }

        *width = image.width;
        *height = image.height;
        *pixelfmt = fmt;
        int bpp = image.samples_per_pixel*image.bits_per_sample/8;
        return flipImage(image.data.data(), image.width, image.height, bpp);
#else
        LOGE("TIFF support disabled - recompile with TANGRAM_TIFF_SUPPORT defined.");
        return nullptr;
#endif
    }

    if (length > 10 && (memcmp(data, "Lerc2 ", 6) == 0 || memcmp(data, "CntZImage ", 10) == 0)) {
#ifdef TANGRAM_LERC_SUPPORT
        USING_NAMESPACE_LERC

        ErrCode err = ErrCode::Ok;
        Lerc::LercInfo info;
        constexpr size_t lerc1hdr = 10 + 4 * sizeof(int) + 1 * sizeof(double);
        if (data[0] == 'C' && length > lerc1hdr) {
            info.RawInit();  // zero out
            // cut and paste from Lerc::GetLercInfo() since that fn reads and discards the entire image to
            //  get min,max values and check for additional bands (which we don't support anyway)
            auto* ptr = data + 10 + 2 * sizeof(int);
            memcpy(&info.nRows, ptr, sizeof(int));  ptr += sizeof(int);
            memcpy(&info.nCols, ptr, sizeof(int));  ptr += sizeof(int);
            memcpy(&info.maxZError, ptr, sizeof(double));  //ptr += sizeof(double);
            info.dt = Lerc::DT_Float;
            info.nDepth = 1;
            info.nBands = 1;
            // assume mask is present so Decode() doesn't fail
            info.nMasks = 1;
        } else {
            err = Lerc::GetLercInfo(data, length, info);
            if (err != ErrCode::Ok) {
                LOGE("Error getting LERC image info: %d", err);
                return nullptr;
            }
        }

        GLint fmt = 0;
        int bpp = info.nDepth;
        if (info.nBands > 1) { // unsupported
        } else if (info.dt == Lerc::DT_Byte || Lerc::DT_Char) {
            if (info.nDepth == 1) { fmt = GL_R8; }
            else if (info.nDepth == 3) { fmt = GL_RGB8; }
            else if (info.nDepth == 4) { fmt = GL_RGBA8; }
        } else if (info.dt == Lerc::DT_Float && info.nDepth == 1) {
            fmt = GL_R32F;
            bpp = sizeof(float);
        }

        if (!fmt) {
            LOGE("Unsupported LERC image: data type %d, depth %d, bands %d", info.dt, info.nDepth, info.nBands);
            return nullptr;
        }

        int w = info.nCols, h = info.nRows;
        //std::unique_ptr<uint8_t, malloc_deleter> pixels((uint8_t*)std::malloc(w * h * bpp));
        std::vector<uint8_t> pixels(w * h * bpp, 0);

        // Lerc::Decode requires mask output if masks present, but we ignore for now
        std::vector<Byte> masks(info.nMasks * w * h, 0);
        Byte* pMasks = info.nMasks > 0 ? masks.data() : nullptr;

        if (info.dt == Lerc::DT_Float) {
            float* fp = (float*)pixels.data();
            err = Lerc::DecodeTempl(fp, data, length, info.nDepth, w, h,
                                    info.nBands, info.nMasks, pMasks, nullptr, nullptr);
            // Tile of all zeros (very small compressed) may be returned instead of 404 - which is actually
            //  helpful since it is deduped in cache and saves us from repeating request for missing tile
            if (err == ErrCode::Ok && std::all_of(fp, fp + w*h, [](float x){ return x == 0.f; })) {
                //LOGD("Discarding all zero LERC tile.");
                return nullptr;
            }
        } else {
            err = Lerc::DecodeTempl(pixels.data(), data, length, info.nDepth, w, h,
                              info.nBands, info.nMasks, pMasks, nullptr, nullptr);
        }

        if (err != ErrCode::Ok) {
            LOGE("Lerc::Decode() failed with error %d", err);
            return nullptr;
        }

        *width = w;
        *height = h;
        *pixelfmt = fmt;
        return flipImage(pixels.data(), w, h, bpp);
#else
        LOGE("LERC support disabled - recompile with TANGRAM_LERC_SUPPORT defined.");
        return nullptr;
#endif
    }

#ifdef TANGRAM_WEBP_SUPPORT
    WebPBitstreamFeatures features;
    uint8_t *img_data, *flipped_img_data;
    if (WebPGetFeatures(data, length, &features) == VP8_STATUS_OK) {
         
        if (channels == 4) {
            img_data = WebPDecodeRGBA(data, length, width, height);
            *pixelfmt = GL_RGBA8;
        } else if (channels == 3){
            img_data = WebPDecodeRGB(data, length, width, height);
            *pixelfmt = GL_RGB8;
        } else {
            LOGE("WebP: Unspported number of components requested (%d)", channels);
            return nullptr;
        }
        
        if (!img_data){
            LOGE("Error loading WebP image data");
            return nullptr;
        }
        
        flipped_img_data = flipImage(img_data, *width, *height, channels);
        WebPFree(img_data);
        return flipped_img_data;
    }
#endif

    
    int channelsInFile = 0;
    std::unique_ptr<uint8_t, malloc_deleter> pixels(
        stbi_load_from_memory(data, int(length), width, height, &channelsInFile, channels));
    if (!pixels) {
        LOGE("Error loading image data: %dx%d bpp:%d/%d - %s",
             width, height, channelsInFile, channels, stbi_failure_reason());
        return nullptr;
    }

    if (channels == 1) *pixelfmt = GL_R8;
    else if (channels == 3) *pixelfmt = GL_RGB8;
    else if (channels == 4) *pixelfmt = GL_RGBA8;

    return flipImage(pixels.get(), *width, *height, channels);
}

}
