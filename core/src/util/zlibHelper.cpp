#include "util/zlibHelper.h"

//#include <zlib.h>
#include "miniz.h"

#include <assert.h>

#define CHUNK 16384

namespace Tangram {

int zlib_inflate(const char* _data, size_t _size, std::vector<char>& dst) {

    int ret;
    unsigned char out[CHUNK];

    z_stream strm;
    memset(&strm, 0, sizeof(z_stream));

#ifdef MZ_DEFAULT_WINDOW_BITS
    // miniz does not handle gzip - we will check for header and assume no extra header data; we can use
    //  miniz_gzip.h for more robust handling in the future
    if (_size < 2) { return -1; }
    uint8_t d1 = (uint8_t)_data[1];
    if (_size > 10 && _data[0] == 0x1F && d1 == 0x8B) {
        // last 4 bytes of gzip file contain uncompressed size
        union { uint8_t bytes[4]; uint32_t dword; } inflsize;
        memcpy(inflsize.bytes, &_data[_size-4], 4);
        dst.reserve(std::min(uint32_t(10*_size), inflsize.dword));
        //if(_data[3] & 0b0001'1110)  LOGE("Extra header fields present");
        _data += 10;
        _size -= 10;  //18;  -- in case footer is missing
    }
    // check for raw zlib header
    else if (_size > 7 && _data[0] == 0x78 && (d1 == 0x01 || d1 == 0x5E || d1 == 0x9C || d1 == 0xDA)) {}
#ifndef TANGRAM_TILE_RAW_DEFLATE
    else { return -1; }
#endif
    ret = inflateInit2(&strm, -MZ_DEFAULT_WINDOW_BITS);
#else
    ret = inflateInit2(&strm, 16+MAX_WBITS);  // +16 to detect and handle gzip
#endif
    if (ret != Z_OK) { return ret; }

    strm.avail_in = _size;
    strm.next_in = (Bytef*)_data;

    do {
        strm.avail_out = CHUNK;
        strm.next_out = out;

        ret = inflate(&strm, Z_NO_FLUSH);

         /* state not clobbered */
        assert(ret != Z_STREAM_ERROR);

        switch (ret) {
        case Z_NEED_DICT:
            ret = Z_DATA_ERROR;
            /* fall through */
        case Z_DATA_ERROR:
        case Z_MEM_ERROR:
            inflateEnd(&strm);
            return ret;
        }

        size_t have = CHUNK - strm.avail_out;
        dst.insert(dst.end(), out, out+have);

    } while (ret == Z_OK);

    inflateEnd(&strm);

    return ret == Z_STREAM_END ? Z_OK : Z_DATA_ERROR;
}

}
