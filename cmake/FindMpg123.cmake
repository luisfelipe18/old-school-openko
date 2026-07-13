# Get Mpg123 package
#
# Makes the libmpg123_wrapper target available.

fetchcontent_declare(
  mpg123
  GIT_REPOSITORY        "https://github.com/Open-KO/mpg123.git"
  GIT_TAG               "v1.33.4-dev"
  GIT_PROGRESS          ON
  GIT_SHALLOW           ON
  SOURCE_SUBDIR         ports/cmake
  EXCLUDE_FROM_ALL
)

set(BUILD_LIBOUT123 OFF CACHE BOOL "mpg123: build libout123 (prerequisite for included programs)")
# set(PORTABLE_API OFF CACHE BOOL "mpg123: Only build portable library API (no off_t, no internal I/O.")
set(BUILD_PROGRAMS OFF CACHE BOOL "mpg123: Build programs (mpg123 executable and others)")

# mpg123's config header has `#cmakedefine DEBUG 1`: any ambient CMake
# variable named DEBUG (some IDE toolchains define one in Debug profiles)
# switches on its extremely chatty per-call stderr logging (readers.c/
# frame.c "debug:" lines on every BGM decode). Force it off - debugging
# mpg123 internals is never what this project's Debug builds are for.
set(DEBUG OFF CACHE BOOL "mpg123: verbose internal debug logging" FORCE)

# Suppress policy warning (Policy CMP194 is not set: MSVC is not an assembler for language ASM.)
get_property(_old_no_dev GLOBAL PROPERTY CMAKE_SUPPRESS_DEVELOPER_WARNINGS)
set_property(GLOBAL PROPERTY CMAKE_SUPPRESS_DEVELOPER_WARNINGS TRUE)

fetchcontent_makeavailable(mpg123)

# Restore developer warnings
set_property(GLOBAL PROPERTY CMAKE_SUPPRESS_DEVELOPER_WARNINGS ${_old_no_dev})

# Setup a wrapper project because it won't expose its paths properly.
add_library(libmpg123_wrapper INTERFACE)
target_link_libraries(libmpg123_wrapper INTERFACE libmpg123)
target_include_directories(libmpg123_wrapper INTERFACE
  $<TARGET_PROPERTY:libmpg123,INCLUDE_DIRECTORIES>
  $<TARGET_PROPERTY:libmpg123,INTERFACE_INCLUDE_DIRECTORIES>
  $<TARGET_PROPERTY:libmpg123,INTERFACE_SYSTEM_INCLUDE_DIRECTORIES>
)

set(mpg123_FOUND TRUE)
