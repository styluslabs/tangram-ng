#include "debug/textDisplay.h"

#include "platform.h"
#include "map.h"
#include "view/view.h"
#include "gl/glError.h"
#include "gl/vertexLayout.h"
#include "gl/renderState.h"
#include "gl/hardware.h"
#include "glm/glm.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "glm/gtx/transform.hpp"
#include <cstdarg>

#define STB_EASY_FONT_IMPLEMENTATION
#include "stb_easy_font.h"

namespace Tangram {

static VertexLayout vertexLayout({{"a_position", 2, GL_FLOAT, false, 0}});

TextDisplay::TextDisplay() : m_margins(0.f), m_initialized(false) {
    m_vertexBuffer = new char[VERTEX_BUFFER_SIZE];
}

TextDisplay::~TextDisplay() {
    if (m_rs && m_VBO) { m_rs->queueBufferDeletion(1, &m_VBO); m_vaos.dispose(*m_rs); }
    delete[] m_vertexBuffer;
}

void TextDisplay::init() {
    if (m_initialized) {
        return;
    }

    std::string vertShaderSrcStr = R"END(
        #ifdef GL_ES
        precision mediump float;
        #endif
        uniform mat4 u_orthoProj;
        attribute vec2 a_position;
        void main() {
            gl_Position = u_orthoProj * vec4(a_position, 0.0, 1.0);
        }
    )END";
    std::string fragShaderSrcStr = R"END(
        #ifdef GL_ES
        precision mediump float;
        #endif
        uniform vec3 u_color;
        void main(void) {
            gl_FragColor = vec4(u_color, 1.0);
        }
    )END";

    m_shader = std::make_unique<ShaderProgram>(vertShaderSrcStr, fragShaderSrcStr, &vertexLayout);

    m_initialized = true;
}

void TextDisplay::deinit() {
    m_shader.reset(nullptr);
    m_initialized = false;
}

void TextDisplay::log(std::string msg) {
    if (!getDebugFlag(DebugFlags::tangram_infos)) { return; }
    std::lock_guard<std::mutex> lock(m_mutex);
    while(m_log.size() >= LOG_CAPACITY) { m_log.pop_back(); }
    m_log.push_front(msg);
}

void TextDisplay::draw(RenderState& rs, const std::string& _text, int _posx, int _posy) {
    std::vector<glm::vec2> vertices;
    int nquads;

    nquads = stb_easy_font_print(_posx, _posy, const_cast<char*>(_text.c_str()), NULL, m_vertexBuffer, VERTEX_BUFFER_SIZE);

    float* data = reinterpret_cast<float*>(m_vertexBuffer);

    vertices.reserve(nquads * 6);
    for (int quad = 0, stride = 0; quad < nquads; ++quad, stride += 16) {
        vertices.push_back({data[(0 * 4) + stride], data[(0 * 4) + 1 + stride]});
        vertices.push_back({data[(1 * 4) + stride], data[(1 * 4) + 1 + stride]});
        vertices.push_back({data[(2 * 4) + stride], data[(2 * 4) + 1 + stride]});
        vertices.push_back({data[(2 * 4) + stride], data[(2 * 4) + 1 + stride]});
        vertices.push_back({data[(3 * 4) + stride], data[(3 * 4) + 1 + stride]});
        vertices.push_back({data[(0 * 4) + stride], data[(0 * 4) + 1 + stride]});
    }
    if (Hardware::supportsVAOs) {
        GL::bufferData(GL_ARRAY_BUFFER, vertices.size()*vertexLayout.getStride(), vertices.data(), GL_STREAM_DRAW);
    } else {
        vertexLayout.enable(rs, *m_shader, 0, (void*)vertices.data());
    }
    GL::drawArrays(GL_TRIANGLES, 0, nquads * 6);
}

void TextDisplay::draw(RenderState& rs, const View& _view, const std::vector<std::string>& _infos) {
    //GLint boundbuffer = -1;

    if (!m_shader->use(rs)) { return; }

    if (Hardware::supportsVAOs) {
        if (!m_vaos.isInitialized()) {
            GL::genBuffers(1, &m_VBO);
            m_vaos.initialize(rs, {{0,0}}, vertexLayout, m_VBO, 0);
            m_rs = &rs;
        }
        m_vaos.bind(0);
    }
    rs.vertexBuffer(m_VBO);

    rs.culling(GL_FALSE);
    rs.blending(GL_FALSE);
    rs.depthTest(GL_FALSE);
    rs.depthMask(GL_FALSE);

    float textScale = _view.getWidth() > _view.getHeight() ? 2 : 1; //2.0f;
    glm::vec4 margins = m_margins/textScale;
    float width = _view.getWidth()/_view.pixelScale()/textScale;
    float height = _view.getHeight()/_view.pixelScale()/textScale;
    glm::mat4 mvp(2/width, 0.f, 0.f, 0.f,
                  0.f, -2/height, 0.f, 0.f,
                  0.f, 0.f, 1.f, 0.f,
                  -1.f, 1.f, 0.f, 1.f);
    m_shader->setUniformMatrix4f(rs, m_uOrthoProj, mvp);

    // Display Tangram info messages
    m_shader->setUniformf(rs, m_uColor, 0.f, 0.f, 0.f);
    int offset = margins[0];
    for (auto& text : _infos) {
        draw(rs, text, margins[3] + 3, 3 + offset);
        offset += 10;
    }

    // Display screen log
    offset = height - margins[2] - 10;
    m_shader->setUniformf(rs, m_uColor, 0.5f, 0.f, 0.f);
    //m_shader->setUniformf(rs, m_uColor, 51 / 255.f, 73 / 255.f, 120 / 255.f);
    {
        std::lock_guard<std::mutex> lock(m_mutex);  // prevent modification during rendering
        for (auto& str : m_log) {
            draw(rs, str, margins[3] + 3, offset);
            offset -= 10;
        }
    }

    rs.culling(GL_TRUE);
    rs.vertexBuffer(0);  //boundbuffer);
    if (m_VBO) { m_vaos.unbind(); }
}

}
