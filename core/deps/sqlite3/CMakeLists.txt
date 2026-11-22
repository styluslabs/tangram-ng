# CMake file for compiling the sqlite3 static library under Windows (for ease of use)
#
# Copyright (c) 2012-2020 Sebastien Rombauts (sebastien.rombauts@gmail.com)
#
# Distributed under the MIT License (MIT) (See accompanying file LICENSE.txt
# or copy at http://opensource.org/licenses/MIT)

# sqlite3.c/.h are downloaded; keep version in sync with core/deps/module.mk
set(SQLITE_VER 3540100)
set(SQLITE_YEAR 2026)
set(SQLITE_ZIP sqlite-amalgamation-${SQLITE_VER})
# convert download version XYYZZ00 to SQLITE_VERSION_NUMBER format XYYYZZZ
math(EXPR SQLITE_VER_NUM "(${SQLITE_VER}/1000000)*1000000 + (${SQLITE_VER}/10000%100)*1000 + ${SQLITE_VER}/100%100")
set(SQLITE_HAVE_VER "")
if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.h")
    file(STRINGS "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.h" SQLITE_HAVE_VER REGEX "^#define SQLITE_VERSION_NUMBER")
    string(REGEX MATCH "[0-9]+" SQLITE_HAVE_VER "${SQLITE_HAVE_VER}")
endif()
if (NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c" OR NOT SQLITE_HAVE_VER STREQUAL SQLITE_VER_NUM)
    file(DOWNLOAD "https://www.sqlite.org/${SQLITE_YEAR}/${SQLITE_ZIP}.zip" "${CMAKE_CURRENT_SOURCE_DIR}/sqlite.zip" STATUS SQLITE_DL_STATUS)
    list(GET SQLITE_DL_STATUS 0 SQLITE_DL_ERR)
    if (SQLITE_DL_ERR)
        message(FATAL_ERROR "sqlite download failed: ${SQLITE_DL_STATUS}")
    endif()
    file(ARCHIVE_EXTRACT INPUT ${CMAKE_CURRENT_SOURCE_DIR}/sqlite.zip DESTINATION ${CMAKE_CURRENT_SOURCE_DIR})
    file(RENAME ${CMAKE_CURRENT_SOURCE_DIR}/${SQLITE_ZIP}/sqlite3.h ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.h)
    file(RENAME ${CMAKE_CURRENT_SOURCE_DIR}/${SQLITE_ZIP}/sqlite3.c ${CMAKE_CURRENT_SOURCE_DIR}/sqlite3.c)
    file(REMOVE_RECURSE ${CMAKE_CURRENT_SOURCE_DIR}/${SQLITE_ZIP} ${CMAKE_CURRENT_SOURCE_DIR}/sqlite.zip)
endif()

# add sources of the "sqlite3" static library
add_library(sqlite3
 sqlite3.c
 sqlite3.h
)

target_include_directories(sqlite3
  PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  PUBLIC $<INSTALL_INTERFACE:include/>)

if (SQLITE_ENABLE_COLUMN_METADATA)
    # Enable the use of SQLite column metadata method
    # Require that the sqlite3 library is also compiled with this flag:
    target_compile_definitions(sqlite3 PUBLIC SQLITE_ENABLE_COLUMN_METADATA)
endif (SQLITE_ENABLE_COLUMN_METADATA)

if (UNIX AND (CMAKE_COMPILER_IS_GNUCXX OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang"))
    set_target_properties(sqlite3 PROPERTIES COMPILE_FLAGS "-fPIC")
endif (UNIX AND (CMAKE_COMPILER_IS_GNUCXX OR ${CMAKE_CXX_COMPILER_ID} STREQUAL "Clang"))

if (UNIX AND CMAKE_COMPILER_IS_GNUCXX)
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 7.0)
        target_compile_options(sqlite3 PRIVATE "-Wimplicit-fallthrough=0")
    endif()
    if (CMAKE_CXX_COMPILER_VERSION VERSION_GREATER 8.0)
        target_compile_options(sqlite3 PRIVATE "-Wno-cast-function-type")
    endif()
endif()

# enable URIs (needs for attaching databases as read-only)
target_compile_definitions(sqlite3 PUBLIC SQLITE_USE_URI=1)
# same options as core/deps/module.mk (see comments there)
target_compile_definitions(sqlite3 PRIVATE SQLITE_OMIT_JSON SQLITE_OMIT_LOAD_EXTENSION SQLITE_OMIT_DEPRECATED
  SQLITE_OMIT_SHARED_CACHE SQLITE_OMIT_PROGRESS_CALLBACK SQLITE_OMIT_DECLTYPE SQLITE_DEFAULT_MEMSTATUS=0
  SQLITE_LIKE_DOESNT_MATCH_BLOBS SQLITE_MAX_EXPR_DEPTH=0 SQLITE_USE_ALLOCA)
# optimize for size in release builds
if (MSVC)
    target_compile_options(sqlite3 PRIVATE $<$<CONFIG:Release,MinSizeRel>:/O1>)
else()
    target_compile_options(sqlite3 PRIVATE $<$<CONFIG:Release,MinSizeRel,RelWithDebInfo>:-Os>)
endif()

# Allow the library to be installed via "make install" and found with "find_package"

include(GNUInstallDirs)
install(TARGETS sqlite3
    EXPORT ${PROJECT_NAME}Targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    COMPONENT libraries)
install(FILES sqlite3.h DESTINATION ${CMAKE_INSTALL_INCLUDEDIR} COMPONENT headers)
