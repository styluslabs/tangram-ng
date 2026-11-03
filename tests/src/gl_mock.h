#pragma once

#include <cstddef>
#include <cstdint>

namespace Tangram {

// GPU memory and upload counters kept by the null GL driver in gl_mock.cpp (main thread only)
struct GLMockStats {
    size_t textureBytes = 0;       // current texture memory (level 0 plus mipmaps)
    size_t bufferBytes = 0;        // current vertex + index buffer memory
    size_t renderbufferBytes = 0;  // current renderbuffer memory
    size_t textureBytesPeak = 0;
    size_t bufferBytesPeak = 0;
    size_t textures = 0;           // live object counts
    size_t buffers = 0;
    uint64_t textureUploads = 0;   // texImage2D + texSubImage2D calls
    uint64_t textureUploadBytes = 0;
    uint64_t bufferUploads = 0;    // bufferData + bufferSubData calls
    uint64_t bufferUploadBytes = 0;
    uint64_t drawCalls = 0;
};

const GLMockStats& glMockStats();

// reset peaks to current values (e.g. at the start of a benchmark phase)
void glMockResetPeaks();

}
