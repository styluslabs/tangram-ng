if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  # Recent versions of Clang are extremely verbose about implicit long-to-int
  # conversions. While this *can* be an actual error, it's not practical to
  # address every instance of it in all dependencies, so we'll silence it here.
  add_compile_options(-Wno-shorten-64-to-32)
endif()

## glm ##
#########
add_library(glm INTERFACE)
target_include_directories(glm INTERFACE glm)
target_compile_definitions(glm INTERFACE GLM_FORCE_CTOR_INIT)

## yaml ##
##############
set(GAML_LIB_ONLY ON CACHE BOOL "")
add_subdirectory(gaml)
#target_include_directories(yaml-cpp PUBLIC yaml-cpp/include)

## css-color-parser-cpp ##
##########################
add_library(css-color-parser-cpp css-color-parser-cpp/csscolorparser.cpp)

set_target_properties(css-color-parser-cpp PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)

target_include_directories(css-color-parser-cpp PUBLIC css-color-parser-cpp)

## duktape ##
#############
add_subdirectory(duktape)


## miniz ##
###########
add_library(miniz
    ${CMAKE_CURRENT_SOURCE_DIR}/miniz/miniz.c)

target_include_directories(miniz
  PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}/miniz)


if(NOT TANGRAM_USE_FONTCONTEXT_STB)

## Harfbuzz - ICU-Common - Freetype2 ##
#######################################
if(NOT TANGRAM_USE_SYSTEM_FONT_LIBS)
  if(TANGRAM_USE_VCPKG)
    find_package(ICU REQUIRED COMPONENTS data)
    find_package(harfbuzz CONFIG REQUIRED)
    set(ALFONS_LIBS harfbuzz::harfbuzz ICU::data)
  else()
    set(HARFBUZZ_BUILD_ICU ON CACHE BOOL "Enable building of ICU")
    add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/harfbuzz-icu-freetype)
    set(ALFONS_LIBS harfbuzz ${HARFBUZZ_LIBRARIES})
  endif()

  set(ALFONS_DEPS_LIBRARIES ${ALFONS_LIBS} CACHE INTERNAL "alfons-libs" FORCE)
endif()

## alfons ##
############
set(GLM_INCLUDE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/glm)
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/alfons)
target_compile_definitions(alfons PRIVATE GLM_FORCE_CTOR_INIT)
if(MSVC)
  # Use a lower warning level on MSVC because -Wall produces excessive output.
  target_compile_options(alfons PUBLIC "/W1")
endif()

endif()


if (TANGRAM_MBTILES_DATASOURCE)
  set(SQLITE_ENABLE_COLUMN_METADATA OFF CACHE BOOL "")

  add_subdirectory(sqlite3)

  target_compile_definitions(sqlite3 PRIVATE SQLITE_ENABLE_FTS5)

  # needed for sqlite3 to work for ndk15c+ and android api level < 21
  # refer:
  # https://github.com/android-ndk/ndk/issues/477 and
  # https://android.googlesource.com/platform/bionic/+/master/docs/32-bit-abi.md
  if (ANDROID)
    target_compile_definitions(sqlite3 PRIVATE _FILE_OFFSET_BITS=32)
  endif()
endif()

## double-conversion ##
#######################
add_subdirectory(double-conversion)

## lerc ##
#######################
set(BUILD_SHARED_LIBS OFF CACHE BOOL "")
add_subdirectory(lerc)


## Prebuild JavascriptCore ##
#############################
if (TANGRAM_USE_JSCORE_STATIC)
  add_library(jscore-static STATIC IMPORTED GLOBAL)
  if(NOT EXISTS "${PROJECT_SOURCE_DIR}/deps/jscore/lib/libJavaScriptCore.a")
    message(SEND_ERROR "Please add static JavaScriptCore libary to core/deps/jscore/")
  endif()
  find_package(ICU COMPONENTS io i18n REQUIRED)
  link_directories(${ICU_LIBRARY_DIRS})

  set(JSCORE_LIBS
    ${ICU_LIBRARIES}
    "${CMAKE_CURRENT_SOURCE_DIR}/jscore/lib/libJavaScriptCore.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/jscore/lib/libWTF.a"
    "${CMAKE_CURRENT_SOURCE_DIR}/jscore/lib/libbmalloc.a")

  set_target_properties(jscore-static
         PROPERTIES
         IMPORTED_LOCATION
         ${JSCORE_LIBS})

  target_link_libraries(jscore-static
    INTERFACE
    ${JSCORE_LIBS})

  target_include_directories(jscore-static
    INTERFACE
    "${CMAKE_CURRENT_SOURCE_DIR}/jscore/include")
endif()
