# Get the FreeType package (POSIX text rendering, docs/PORT_POSIX_PLAN.md T7.1)
#
# Prefers a system/package-manager FreeType (Homebrew, apt, vcpkg, ...);
# falls back to fetching and building it from source.
# Makes the Freetype::Freetype target available.
#
# Named FindFreetypeFont to avoid shadowing CMake's builtin FindFreetype
# module, which this file delegates to for the system lookup.

find_package(Freetype QUIET)

if(FREETYPE_FOUND)
  message(STATUS "OpenKO: [FindFreetypeFont] Using system FreeType ${FREETYPE_VERSION_STRING}")
else()
  message(STATUS "OpenKO: [FindFreetypeFont] No system FreeType found; fetching and building from source.")

  fetchcontent_declare(
    freetype
    GIT_REPOSITORY        "https://gitlab.freedesktop.org/freetype/freetype.git"
    GIT_TAG               "VER-2-13-3"
    GIT_PROGRESS          ON
    GIT_SHALLOW           ON
    EXCLUDE_FROM_ALL
  )

  # Rasterization only: no optional codecs/shaping needed for the glyph atlas.
  set(FT_DISABLE_ZLIB ON CACHE BOOL "FreeType: use internal zlib")
  set(FT_DISABLE_BZIP2 ON CACHE BOOL "FreeType: no bzip2")
  set(FT_DISABLE_PNG ON CACHE BOOL "FreeType: no png")
  set(FT_DISABLE_HARFBUZZ ON CACHE BOOL "FreeType: no harfbuzz")
  set(FT_DISABLE_BROTLI ON CACHE BOOL "FreeType: no brotli")
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "FreeType: static")

  fetchcontent_makeavailable(freetype)

  # The source build exports the target as "freetype"; alias it to the name
  # the builtin Find module provides so consumers link one target.
  if(NOT TARGET Freetype::Freetype)
    add_library(Freetype::Freetype ALIAS freetype)
  endif()

  set(FREETYPE_FOUND TRUE)
endif()

set(FreetypeFont_FOUND TRUE)
