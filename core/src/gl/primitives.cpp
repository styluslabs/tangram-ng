#include "primitives.h"

#include "glm/mat4x4.hpp"
#include "glm/gtc/matrix_transform.hpp"
#include "gl/glError.h"
#include "gl/shaderProgram.h"
#include "gl/vertexLayout.h"
#include "gl/renderState.h"
#include "gl/texture.h"
#include "gl/hardware.h"
#include "log.h"

#include "debugPrimitive_vs.h"
#include "debugPrimitive_fs.h"

#include "debugTexture_vs.h"
#include "debugTexture_fs.h"

namespace Tangram {

namespace Primitives {

static bool s_initialized;
static std::unique_ptr<ShaderProgram> s_shader;
static std::unique_ptr<VertexLayout> s_layout;

static UniformLocation s_uColor{"u_color"};
static UniformLocation s_uProj{"u_proj"};


static std::unique_ptr<ShaderProgram> s_textureShader;
static std::unique_ptr<VertexLayout> s_textureLayout;

static UniformLocation s_uTextureProj{"u_proj"};
static UniformLocation s_uTextureScale{"u_scale"};

static GLuint s_VAO = 0;
static GLuint s_VBO = 0;

void init() {

    // lazy init
    if (!s_initialized) {
        s_layout = std::unique_ptr<VertexLayout>(new VertexLayout({
            {"a_position", 2, GL_FLOAT, false, 0},
        }));
        s_shader = std::make_unique<ShaderProgram>(debugPrimitive_vs, debugPrimitive_fs, s_layout.get());

        s_textureLayout = std::unique_ptr<VertexLayout>(new VertexLayout({
            {"a_position", 2, GL_FLOAT, false, 0},
            {"a_uv", 2, GL_FLOAT, false, 0},
        }));
        s_textureShader = std::make_unique<ShaderProgram>(debugTexture_vs, debugTexture_fs, s_textureLayout.get());

        s_initialized = true;
        //GL::lineWidth(1.5f);  -- not supported in GL 3 core
    }

    if (Hardware::supportsVAOs && !s_VAO) {
        GL::genBuffers(1, &s_VBO);
        GL::genVertexArrays(1, &s_VAO);
    }
}

void deinit() {

    if (s_VAO) {
        GL::deleteBuffers(1, &s_VBO);  s_VBO = 0;
        GL::deleteVertexArrays(1, &s_VAO);  s_VAO = 0;
    }
    s_shader.reset(nullptr);
    s_layout.reset(nullptr);
    s_textureShader.reset(nullptr);
    s_textureLayout.reset(nullptr);
    s_initialized = false;
}

void drawPoly(RenderState& rs, const glm::vec2* _polygon, size_t _n) {
    init();

    if (!s_shader->use(rs)) { return; }

    rs.depthTest(GL_FALSE);
    rs.vertexBuffer(s_VBO);
    if (s_VAO) {
        GL::bindVertexArray(s_VAO);
        s_layout->enable(0);
        GL::bufferData(GL_ARRAY_BUFFER, _n*s_layout->getStride(), _polygon, GL_STREAM_DRAW);
    } else {
        s_layout->enable(rs, *s_shader, 0, (void*)_polygon);
    }

    GL::drawArrays(_n > 2 ? GL_LINE_LOOP : GL_LINES, 0, _n);

    if (s_VAO) { GL::bindVertexArray(0); }
    rs.vertexBuffer(0);
}

void drawLine(RenderState& rs, const glm::vec2& _origin, const glm::vec2& _destination) {
    glm::vec2 verts[2] = { _origin, _destination };
    drawPoly(rs, verts, 2);
}

void drawRect(RenderState& rs, const glm::vec2& _origin, const glm::vec2& _destination) {
    glm::vec2 verts[4] =
        { _origin, {_destination.x, _origin.y}, _destination, {_origin.x, _destination.y} };
    drawPoly(rs, verts, 4);
}

void drawTexture(RenderState& rs, Texture& _tex, glm::vec2 _pos, glm::vec2 _dim, float _scale) {
    init();

    if (!s_textureShader->use(rs)) { return; }

    s_textureShader->setUniformf(rs, s_uTextureScale, _scale);

    float w = _tex.width();
    float h = _tex.height();

    if (_dim != glm::vec2(0)) {
        w = _dim.x;
        h = _dim.y;
    }
    glm::vec4 vertices[6] = {
        {_pos.x, _pos.y, 0, 1},
        {_pos.x, _pos.y + h, 0, 0},
        {_pos.x + w, _pos.y, 1, 1},

        {_pos.x + w, _pos.y, 1, 1},
        {_pos.x, _pos.y + h, 0, 0},
        {_pos.x + w, _pos.y + h, 1, 0},
    };

    _tex.bind(rs, 0);

    rs.depthTest(GL_FALSE);
    rs.vertexBuffer(s_VBO);
    if (s_VAO) {
        GL::bindVertexArray(s_VAO);
        s_textureLayout->enable(0);
        GL::bufferData(GL_ARRAY_BUFFER, 6*s_textureLayout->getStride(), vertices, GL_STREAM_DRAW);
    } else {
        s_textureLayout->enable(rs, *s_textureShader, 0, (void*)vertices);
    }

    GL::drawArrays(GL_TRIANGLES, 0, 6);

    if (s_VAO) { GL::bindVertexArray(0); }
    rs.vertexBuffer(0);
}

void setColor(RenderState& rs, unsigned int _color) {
    init();

    float r = (_color >> 16 & 0xff) / 255.0;
    float g = (_color >> 8  & 0xff) / 255.0;
    float b = (_color       & 0xff) / 255.0;

    s_shader->setUniformf(rs, s_uColor, r, g, b);
}

void setResolution(RenderState& rs, float _width, float _height) {
    init();

    glm::mat4 proj = glm::ortho(0.f, _width, _height, 0.f, -1.f, 1.f);
    s_shader->setUniformMatrix4f(rs, s_uProj, proj);
    s_shader->setUniformf(rs, s_uColor, 1.0f, 1.0f, 1.0f);  // reset color
    s_textureShader->setUniformMatrix4f(rs, s_uTextureProj, proj);
}

}

}
