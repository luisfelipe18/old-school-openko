# Get miniz package
#
# Makes the `miniz` target available - a single-file, public-domain ZIP
# reader/writer used by the POSIX Launcher to extract patch archives
# (docs/PORT_POSIX_PLAN.md, F10). The Windows Launcher uses the MFC-only
# ZipArchive instead; miniz is the portable replacement.

fetchcontent_declare(
  miniz
  GIT_REPOSITORY        "https://github.com/richgel999/miniz.git"
  GIT_TAG               "3.0.2"
  GIT_PROGRESS          ON
  GIT_SHALLOW           ON
  EXCLUDE_FROM_ALL
)

# miniz's own CMake generates an amalgamated miniz.c/miniz.h and builds a
# library target; we just need that target available.
set(BUILD_EXAMPLES OFF CACHE BOOL "miniz: build examples")
set(BUILD_FUZZERS OFF CACHE BOOL "miniz: build fuzzers")
set(BUILD_HEADER_ONLY OFF CACHE BOOL "miniz: header-only build")
set(BUILD_SHARED_LIBS OFF CACHE BOOL "miniz: build shared library")
set(INSTALL_PROJECT OFF CACHE BOOL "miniz: install project")

get_property(_old_no_dev GLOBAL PROPERTY CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
set_property(GLOBAL PROPERTY CMAKE_SUPPRESS_DEVELOPER_WARNINGS TRUE)

fetchcontent_makeavailable(miniz)

set_property(GLOBAL PROPERTY CMAKE_SUPPRESS_DEVELOPER_WARNINGS ${_old_no_dev})

# miniz's amalgamated headers land in its binary dir; expose them so
# `#include <miniz.h>` works for consumers of the target.
if(TARGET miniz)
  target_include_directories(miniz PUBLIC
    "${miniz_SOURCE_DIR}"
    "${miniz_BINARY_DIR}"
    "${miniz_BINARY_DIR}/amalgamation")
endif()

set(miniz_FOUND TRUE)
