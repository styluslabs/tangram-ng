#define NANOSVG_IMPLEMENTATION
#include "nanosvg/nanosvg.h"
#define NANOSVGRAST_IMPLEMENTATION
#include "nanosvg/nanosvgrast.h"

#define FONTSTASH_IMPLEMENTATION
#include "fontstash/fontstash.h"

#include "scene/styleContext.h"
#include "gl/texture.h"

#ifdef TANGRAM_ANDROID_MAIN
void TANGRAM_WakeEventLoop() {}
#endif

//#include "log.h"
#ifndef GLM_FORCE_CTOR_INIT
#error "GLM_FORCE_CTOR_INIT must be defined!"
#endif

namespace Tangram {

FONScontext* userCreateFontstash(FONSparams* params, int atlasFontPx) { return fonsCreateInternal(params); }

NativeStyleFn userGetStyleFunction(Scene& scene, const std::string& jsSource) { return {}; }

bool userLoadSvg(const char* svg, size_t len, Texture* texture)
{
  std::string svgStr(svg, len);
  NSVGimage* image = nsvgParse((char*)svgStr.c_str(), "px", 192.0f);  // nsvgParse is destructive
  if (!image) { return false; }
  nsvgApplyViewXform(image);

  auto atlas = std::make_unique<SpriteAtlas>();
  bool hasSprites = false;
  SpriteNode spritenode;
  NSVGshape* shape = image->shapes;
  while(shape) {
    // nanosvg treats id like other attributes, propagating to children, so combine bounds of shapes with same id
    if(shape->id[0] && (shape->flags & NSVG_FLAGS_VISIBLE)) {
      hasSprites = true;
      glm::vec2 pos(shape->bounds[0], shape->bounds[1]);
      glm::vec2 size(shape->bounds[2] - shape->bounds[0], shape->bounds[3] - shape->bounds[1]);
      if(atlas->getSpriteNode(shape->id, spritenode)) {
        glm::vec2 lr = glm::max(pos + size, spritenode.m_origin + spritenode.m_size);
        pos = glm::min(pos, spritenode.m_origin);
        size = lr - pos;
      }
      atlas->addSpriteNode(shape->id, pos, size);
    }
    shape = shape->next;
  }
  if(hasSprites)
    texture->setSpriteAtlas(std::move(atlas));

  float scale = 1.0;
  int w = int(image->width*scale + 0.5f);
  int h = int(image->height*scale + 0.5f);
  NSVGrasterizer* rast = nsvgCreateRasterizer();
  if (!rast) return false;  // OOM, so we don't care about NSVGimage leak
  std::vector<uint8_t> img(w*h*4, 0);
  // note the hack to flip y-axis - should be moved into nanosvgrast.h, activated w/ h < 0
  nsvgRasterize(rast, image, 0, 0, scale, &img[w*(h-1)*4], w, h, -w*4);
  nsvgDelete(image);
  nsvgDeleteRasterizer(rast);
  texture->setPixelData(w, h, 4, img.data(), img.size());
  return true;
}

}
