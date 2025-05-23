#pragma once

#include "gl.h"
#include <vector>
#include <string>
#include <cstdint>

namespace Tangram {

class RenderState;
class ShaderProgram;
class VertexLayout;

using VertexOffsets = std::vector<std::pair<uint32_t, uint32_t>>;

class Vao {

public:

    void initialize(RenderState& rs, const VertexOffsets& _vertexOffsets,
                    VertexLayout& _layout, GLuint _vertexBuffer, GLuint _indexBuffer);
    bool isInitialized();
    void bind(unsigned int _index);
    void unbind();
    void dispose(RenderState& rs);

private:
    std::vector<GLuint> m_glVAOs;

};

}

