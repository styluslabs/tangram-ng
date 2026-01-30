# tangram-es external dependencies

MAKE_BASE := $(GET_MAKE_BASE)

## header-only libraries
INC += \
  $(MAKE_BASE) \
  $(MAKE_BASE)/earcut/include \
  $(MAKE_BASE)/isect2d/include \
  $(MAKE_BASE)/mapbox \
  $(MAKE_BASE)/pbf \
  $(MAKE_BASE)/stb \
  $(MAKE_BASE)/variant/include

#hash-library rapidjson


## css-color-parser-cpp
MODULE_BASE = $(MAKE_BASE)/css-color-parser-cpp

MODULE_SOURCES = csscolorparser.cpp
MODULE_INC_PUBLIC = .
#MODULE_CFLAGS = -DTHIS_IS_CSS_COLOR

include $(ADD_MODULE)


## double-conversion
MODULE_BASE = $(MAKE_BASE)/double-conversion

MODULE_SOURCES = \
  src/bignum-dtoa.cc \
  src/bignum.cc \
  src/cached-powers.cc \
  src/diy-fp.cc \
  src/double-conversion.cc \
  src/fast-dtoa.cc \
  src/fixed-dtoa.cc \
  src/strtod.cc

MODULE_INC_PUBLIC = include
MODULE_INC_PRIVATE = src

include $(ADD_MODULE)


## duktape
MODULE_BASE = $(MAKE_BASE)/duktape

MODULE_SOURCES = duktape.c
#MODULE_INC_PUBLIC = .
MODULE_CCFLAGS = -fstrict-aliasing -fomit-frame-pointer -std=c99 -Wall

include $(ADD_MODULE)


## geojson-vt-cpp
MODULE_BASE = $(MAKE_BASE)/geojson-vt-cpp

MODULE_INC_PUBLIC = include

include $(ADD_MODULE)


## glm
MODULE_BASE = $(MAKE_BASE)/glm

MODULE_INC_PUBLIC = .
MODULE_DEFS_PUBLIC = GLM_FORCE_CTOR_INIT

include $(ADD_MODULE)


## miniz
MODULE_BASE = $(MAKE_BASE)/miniz

MODULE_SOURCES = miniz.c
MODULE_INC_PUBLIC = .

include $(ADD_MODULE)


## sqlite
MODULE_BASE = $(MAKE_BASE)/sqlite3

MODULE_SOURCES = sqlite3.c
MODULE_INC_PUBLIC = .
MODULE_DEFS_PRIVATE = SQLITE_ENABLE_FTS5
MODULE_DEFS_PUBLIC = SQLITE_USE_URI=1

include $(ADD_MODULE)

# if only sqlite3.h is included here, first run of make will fail to build sqlite3.o
SQLITE_BASE := $(MODULE_BASE)
SQLITE_GEN := $(SQLITE_BASE)/sqlite3.h $(SQLITE_BASE)/sqlite3.c
GENERATED += $(SQLITE_GEN)

ifneq ($(windir),)
$(SQLITE_GEN):
	cd $(SQLITE_BASE) && curl "https://www.sqlite.org/2020/sqlite-amalgamation-3320300.zip" -o sqlite.zip && tar -xf sqlite.zip && move sqlite-amalgamation-3320300\sqlite3.* .
else
$(SQLITE_GEN):
	cd $(SQLITE_BASE) && curl "https://www.sqlite.org/2020/sqlite-amalgamation-3320300.zip" -o sqlite.zip && unzip sqlite.zip && mv sqlite-amalgamation-3320300/sqlite3.* .
endif

## gaml
MODULE_BASE = $(MAKE_BASE)/gaml

MODULE_SOURCES = src/yaml.cpp
#MODULE_INC_PUBLIC = include
MODULE_INC_PRIVATE = src ../double-conversion/include ../../include/tangram
MODULE_DEFS_PRIVATE = GAML_LIB_ONLY GAML_DOUBLE_CONV=1 GAML_LOG=LOGE
ifneq ($(windir),)
MODULE_CFLAGS = /FI log.h
else
MODULE_CFLAGS = -include log.h
endif

include $(ADD_MODULE)


## LERC
MODULE_BASE = $(MAKE_BASE)/lerc/src/LercLib

MODULE_FULL_SOURCES = $(wildcard $(MODULE_BASE)/*.cpp)
MODULE_FULL_SOURCES += $(wildcard $(MODULE_BASE)/Lerc1Decode/*.cpp)

MODULE_INC_PRIVATE = Lerc1Decode
MODULE_INC_PUBLIC = .

include $(ADD_MODULE)

ifeq ($(windir),) ## this section only works for unix-like systems 
## WebP
MODULE_BASE = $(MAKE_BASE)/libwebp
MODULE_INC_PRIVATE = src
WEBP_GEN := libwebp.a
GENERATED += $(WEBP_GEN)
LIBS += -L $(MAKE_BASE)/libwebp/src -lwebp

include $(ADD_MODULE)
endif

libwebp.a:
	$(MAKE) -C $(MAKE_BASE)/libwebp -f makefile.unix src/libwebp.a

