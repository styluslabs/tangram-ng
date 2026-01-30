MAKE_BASE := $(GET_MAKE_BASE)

MODULE_BASE = $(MAKE_BASE)

MODULE_SOURCES = \
  src/map.cpp                         \
  src/platform.cpp                    \
  src/data/clientDataSource.cpp       \
  src/data/memoryCacheDataSource.cpp  \
  src/data/networkDataSource.cpp      \
  src/data/properties.cpp             \
  src/data/rasterSource.cpp           \
  src/data/tileSource.cpp             \
  src/data/formats/geoJson.cpp        \
  src/data/formats/mvt.cpp            \
  src/data/formats/topoJson.cpp       \
  src/debug/frameInfo.cpp             \
  src/debug/textDisplay.cpp           \
  src/gl/framebuffer.cpp              \
  src/gl/glError.cpp                  \
  src/gl/glyphTexture.cpp             \
  src/gl/hardware.cpp                 \
  src/gl/mesh.cpp                     \
  src/gl/primitives.cpp               \
  src/gl/renderState.cpp              \
  src/gl/shaderProgram.cpp            \
  src/gl/shaderSource.cpp             \
  src/gl/texture.cpp                  \
  src/gl/vao.cpp                      \
  src/gl/vertexLayout.cpp             \
  src/labels/curvedLabel.cpp          \
  src/labels/label.cpp                \
  src/labels/labelCollider.cpp        \
  src/labels/labelProperty.cpp        \
  src/labels/labelSet.cpp             \
  src/labels/labelManager.cpp         \
  src/labels/spriteLabel.cpp          \
  src/labels/textLabel.cpp            \
  src/marker/marker.cpp               \
  src/marker/markerManager.cpp        \
  src/scene/ambientLight.cpp          \
  src/scene/dataLayer.cpp             \
  src/scene/directionalLight.cpp      \
  src/scene/drawRule.cpp              \
  src/scene/filters.cpp               \
  src/scene/importer.cpp              \
  src/scene/light.cpp                 \
  src/scene/pointLight.cpp            \
  src/scene/scene.cpp                 \
  src/scene/sceneLayer.cpp            \
  src/scene/sceneLoader.cpp           \
  src/scene/spotLight.cpp             \
  src/scene/spriteAtlas.cpp           \
  src/scene/stops.cpp                 \
  src/scene/styleContext.cpp          \
  src/scene/styleMixer.cpp            \
  src/scene/styleParam.cpp            \
  src/selection/featureSelection.cpp  \
  src/selection/selectionQuery.cpp    \
  src/style/debugStyle.cpp            \
  src/style/debugTextStyle.cpp        \
  src/style/material.cpp              \
  src/style/pointStyle.cpp            \
  src/style/pointStyleBuilder.cpp     \
  src/style/polygonStyle.cpp          \
  src/style/polylineStyle.cpp         \
  src/style/rasterStyle.cpp           \
  src/style/style.cpp                 \
  src/style/textStyle.cpp             \
  src/style/textStyleBuilder.cpp      \
  src/style/contourTextStyle.cpp      \
  src/text/fontContext.cpp            \
  src/text/textUtil.cpp               \
  src/tile/tile.cpp                   \
  src/tile/tileBuilder.cpp            \
  src/tile/tileManager.cpp            \
  src/tile/tileTask.cpp               \
  src/tile/tileWorker.cpp             \
  src/util/builders.cpp               \
  src/util/dashArray.cpp              \
  src/util/elevationManager.cpp       \
  src/util/extrude.cpp                \
  src/util/floatFormatter.cpp         \
  src/util/geom.cpp                   \
  src/util/inputHandler.cpp           \
  src/util/jobQueue.cpp               \
  src/util/json.cpp                   \
  src/util/mapProjection.cpp          \
  src/util/skyManager.cpp             \
  src/util/stbImage.cpp               \
  src/util/url.cpp                    \
  src/util/util.cpp                   \
  src/util/yamlPath.cpp               \
  src/util/yamlUtil.cpp               \
  src/util/zipArchive.cpp             \
  src/util/zlibHelper.cpp             \
  src/view/flyTo.cpp                  \
  src/view/view.cpp                   \
  src/view/viewConstraint.cpp

MODULE_SOURCES += src/js/DuktapeContext.cpp
MODULE_SOURCES += src/data/mbtilesDataSource.cpp

MODULE_INC_PUBLIC = include/tangram src
MODULE_INC_PRIVATE = generated
MODULE_DEFS_PUBLIC = FONTCONTEXT_STB=1 TANGRAM_MBTILES_DATASOURCE=1

ifeq ($(windir),)
MODULE_DEFS_PUBLIC += TANGRAM_WEBP_SUPPORT
endif

# shaders
SHADER_HDRS = \
  generated/ambientLight_glsl.h     \
  generated/debug_fs.h              \
  generated/debug_vs.h              \
  generated/debugPrimitive_fs.h     \
  generated/debugPrimitive_vs.h     \
  generated/debugTexture_fs.h       \
  generated/debugTexture_vs.h       \
  generated/directionalLight_glsl.h \
  generated/lights_glsl.h           \
  generated/material_glsl.h         \
  generated/point_fs.h              \
  generated/point_vs.h              \
  generated/pointLight_glsl.h       \
  generated/polygon_fs.h            \
  generated/polygon_vs.h            \
  generated/polyline_fs.h           \
  generated/polyline_vs.h           \
  generated/rasters_glsl.h          \
  generated/sdf_fs.h                \
  generated/selection_fs.h          \
  generated/spotLight_glsl.h        \
  generated/text_fs.h               \
  generated/text_vs.h

ifneq ($(windir),)
GLSL_PREFIX=R"RAW_GLSL(
GLSL_SUFFIX=)RAW_GLSL";
$(MODULE_BASE)/generated/%_vs.h: $(MODULE_BASE)/shaders/%.vs
	$(file >$@,static const char* $*_vs = $(GLSL_PREFIX)$(file <$<)$(GLSL_SUFFIX))

$(MODULE_BASE)/generated/%_fs.h: $(MODULE_BASE)/shaders/%.fs
	$(file >$@,static const char* $*_fs = $(GLSL_PREFIX)$(file <$<)$(GLSL_SUFFIX))

$(MODULE_BASE)/generated/%_glsl.h: $(MODULE_BASE)/shaders/%.glsl
	$(file >$@,static const char* $*_glsl = $(GLSL_PREFIX)$(file <$<)$(GLSL_SUFFIX))
else
$(MODULE_BASE)/generated/%_vs.h: $(MODULE_BASE)/shaders/%.vs
	(echo 'static const char* $*_vs = R"RAW_GLSL('; cat $<; echo ')RAW_GLSL";') > $@

$(MODULE_BASE)/generated/%_fs.h: $(MODULE_BASE)/shaders/%.fs
	(echo 'static const char* $*_fs = R"RAW_GLSL('; cat $<; echo ')RAW_GLSL";') > $@

$(MODULE_BASE)/generated/%_glsl.h: $(MODULE_BASE)/shaders/%.glsl
	(echo 'static const char* $*_glsl = R"RAW_GLSL('; cat $<; echo ')RAW_GLSL";') > $@
endif

include $(ADD_MODULE)

# existence only dependency since actual shader header dependencies are in .d files
$(MODULE_OBJS): | $(SHADER_HDRS:%=$(MODULE_BASE)/%)

$(OBJDIR)/$(MODULE_BASE)/src/text/fontContext.$(OBJEXT): INC_PRIVATE := $(MODULE_BASE)/../../$(STYLUSLABS_DEPS)/nanovgXC/src

# dependencies

include $(MAKE_BASE)/deps/module.mk
