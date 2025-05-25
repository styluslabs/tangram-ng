if (CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-gnu-zero-variadic-macro-arguments")
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -lc++ -lc++abi")
endif()

if (CMAKE_COMPILER_IS_GNUCC)
  message(STATUS "Using gcc ${CMAKE_CXX_COMPILER_VERSION}")
  if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 5.1)
    message(STATUS "USE CXX11_ABI")
    add_definitions("-D_GLIBCXX_USE_CXX11_ABI=1")
  endif()
  if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-strict-aliasing")
  endif()
endif()

# -Wshadow ... too many in Tangram to deal with right now
add_compile_options(-Werror=return-type)

check_unsupported_compiler_version()

add_definitions(-DTANGRAM_LINUX)

set(OpenGL_GL_PREFERENCE GLVND)
find_package(OpenGL REQUIRED)

include(cmake/glfw.cmake)

# System font config
include(FindPkgConfig)
pkg_check_modules(FONTCONFIG REQUIRED "fontconfig")

find_package(CURL REQUIRED)

add_executable(tangram
  platforms/linux/src/linuxPlatform.cpp
  platforms/common/platform_gl.cpp
  platforms/common/imgui_impl_glfw.cpp
  platforms/common/imgui_impl_opengl3.cpp
  platforms/common/urlClient.cpp
  platforms/common/linuxSystemFontHelper.cpp
  platforms/common/glfwApp.cpp
  platforms/common/user_fns.cpp
  platforms/linux/src/main.cpp
)

add_subdirectory(platforms/common/imgui)

target_include_directories(tangram
  PRIVATE
  platforms/common
  core/deps/glm
  core/deps/isect2d/include
  ${FONTCONFIG_INCLUDE_DIRS}
)

# dependencies should come after modules that depend on them (remember to try start-group / end-group to
#  investigate unexplained "undefined reference" errors)
target_link_libraries(tangram
  PRIVATE
  tangram-core
  glfw
  imgui
  ${GLFW_LIBRARIES}
  ${OPENGL_LIBRARIES}
  ${FONTCONFIG_LDFLAGS}
  ${CURL_LIBRARIES}
  -pthread
  # only used when not using external lib
  -ldl
)

target_compile_options(tangram
  PRIVATE
  -std=c++14
  -fno-omit-frame-pointer
  -Wall
  -Wreturn-type
  -Wsign-compare
  -Wignored-qualifiers
  -Wtype-limits
  -Wmissing-field-initializers
)

# tracing
if(CMAKE_BUILD_TYPE MATCHES RelWithDebInfo)
  target_compile_definitions(tangram-core PRIVATE TANGRAM_TRACING=1)
  target_compile_definitions(tangram-core PRIVATE TANGRAM_JS_TRACING=1)
endif()

#get_nextzen_api_key(NEXTZEN_API_KEY)
#target_compile_definitions(tangram
#  PRIVATE
#  NEXTZEN_API_KEY="${NEXTZEN_API_KEY}")

# to be consistent w/ core
target_compile_definitions(tangram PRIVATE GLM_FORCE_CTOR_INIT)

#add_resources(tangram "${PROJECT_SOURCE_DIR}/scenes" "res")
