cmake_minimum_required(VERSION 3.10)

option(GAML_LIB_ONLY "Library only" OFF)

if(GAML_LIB_ONLY)
  add_library(gaml src/yaml.cpp src/yaml.h)
  target_include_directories(gaml PRIVATE ../double-conversion/include ../../include/tangram)
  target_compile_definitions(gaml PRIVATE GAML_DOUBLE_CONV=1 GAML_LIB_ONLY=1 GAML_LOG=LOGE)
  target_compile_options(gaml PRIVATE -include log.h)
else()
  project(gaml)

  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -std=c++11 -fno-rtti -fno-exceptions")
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -stdlib=libc++")
  endif()

  add_compile_options(-Wall -Wextra)

  add_library(gaml STATIC src/yaml.cpp)
  link_libraries(gaml)
  add_executable(test-suite src/test-suite.cpp)
  add_executable(gamlpp src/pretty-print.cpp)
  add_executable(benchmark src/benchmark.cpp)
  target_include_directories(benchmark PRIVATE rapidjson/include)
endif()
