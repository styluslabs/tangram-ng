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
# used on all platforms, incl. iOS (system SQLite is not used); keep version and options in sync with
#  sqlite3/CMakeLists.txt
MODULE_BASE = $(MAKE_BASE)/sqlite3

MODULE_SOURCES = sqlite3.c
MODULE_INC_PUBLIC = .
# - FTS5 for offline search; URI filenames (e.g. for attaching databases read-only)
# - OMIT_JSON: JSON SQL fns (built in since 3.38) not used, ~60 KB; OMIT_LOAD_EXTENSION: table of API fns for
#  extensions references every API fn, preventing --gc-sections from removing unused ones, ~60 KB
# - rest are recommended options from sqlite.org/compile.html#rcmd (note DEFAULT_MEMSTATUS=0 makes
#  sqlite3_memory_used() etc. return 0); other OMIT options tested (UTF16, DESERIALIZE, TRACE, ...) saved ~15 KB
MODULE_DEFS_PRIVATE = SQLITE_ENABLE_FTS5 SQLITE_OMIT_JSON SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED \
  SQLITE_OMIT_SHARED_CACHE SQLITE_OMIT_PROGRESS_CALLBACK SQLITE_OMIT_DECLTYPE SQLITE_DEFAULT_MEMSTATUS=0 \
  SQLITE_LIKE_DOESNT_MATCH_BLOBS SQLITE_MAX_EXPR_DEPTH=0 SQLITE_USE_ALLOCA
MODULE_DEFS_PUBLIC = SQLITE_USE_URI=1
# optimize for size in release builds: ~30% smaller than -O2, <5% slower for our queries (linked size, x86-64:
#  3.54.1 -O2 1423 KB, w/ above options and -Os 917 KB); OMIT options leave some unused static fns
ifneq ($(windir),)
MODULE_CFLAGS = $(if $(filter-out 0,$(DEBUG)),,/O1)
else
MODULE_CFLAGS = -Wno-unused-function $(if $(filter-out 0,$(DEBUG)),,-Os)
endif

# sqlite3.c/.h are downloaded (again when version changes)
SQLITE_ZIP := sqlite-amalgamation-3540100
$(call FETCH_FILES,$(MODULE_BASE),https://www.sqlite.org/2026/$(SQLITE_ZIP).zip,$(SQLITE_ZIP)/sqlite3.c $(SQLITE_ZIP)/sqlite3.h)

include $(ADD_MODULE)

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
