// WUFFS must be compiled as C for now due to a few missing casts from void* with WUFFS_CONFIG__ENABLE_DROP_IN_REPLACEMENT__STB

// github.com/google/wuffs vs stb:
// - PNG decode is faster
// - JPEG decode slower in valgrind but actual time is equal on Linux and faster on Android (1.3ms vs 2ms avg)
// - deflate (zip/zlib/gzip) decoder is slightly faster in valgrind; actual time is faster on Linux (0.75ms vs 1ms)
// See tangram-es/bench/benchmark/src/timers.cc for how to get per-thread CPU time

#ifndef TANGRAM_NO_WUFFS

#define WUFFS_IMPLEMENTATION
#include "wuffs.h"

// stb_image fns used in nanovg but not implemented in WUFFS
void stbi_set_unpremultiply_on_load(int flag_true_if_should_unpremultiply) {}
void stbi_convert_iphone_png_to_rgb(int flag_true_if_should_convert) {}

#endif

/*
struct Tracer {
    static uint64_t thread_cpu_time_ns() {
      struct timespec ts{};
      if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) != 0) return 0;
      return ts.tv_sec * 1000000000ULL + ts.tv_nsec;
    }

    uint64_t m_t0 = thread_cpu_time_ns();
    static size_t nbytes;
    static uint64_t ttot;
    static size_t ntot;
    Tracer(size_t len) { nbytes += len; }

    void finish() {
        uint64_t t1 = thread_cpu_time_ns();
        ttot += t1 - m_t0;
        ntot++;
        if(ntot%10 == 0)
          LOG("Tangram::loadImage(): %.0f us (%d images, %d bytes in)", 1E-3*ttot/ntot, ntot, nbytes);
    }
};

size_t Tracer::nbytes = 0;
uint64_t Tracer::ttot = 0;
size_t Tracer::ntot = 0;
*/
