# Get SDL3 package
#
# Prefers a system/package-manager SDL3 (Homebrew, apt, vcpkg, ...);
# falls back to fetching and building it from source.
# Makes the SDL3::SDL3 target available.

# CONFIG mode explicitly avoids re-entering this module.
find_package(SDL3 CONFIG QUIET)

if(SDL3_FOUND)
  message(STATUS "OpenKO: [FindSDL3] Using system SDL3 ${SDL3_VERSION}")
else()
  message(STATUS "OpenKO: [FindSDL3] No system SDL3 found; fetching and building from source.")

  fetchcontent_declare(
    sdl3
    GIT_REPOSITORY        "https://github.com/libsdl-org/SDL.git"
    GIT_TAG               "release-3.2.30"
    GIT_PROGRESS          ON
    GIT_SHALLOW           ON
    EXCLUDE_FROM_ALL
  )

  set(SDL_SHARED OFF CACHE BOOL "SDL3: Build a shared version of the library")
  set(SDL_STATIC ON CACHE BOOL "SDL3: Build a static version of the library")
  set(SDL_TEST_LIBRARY OFF CACHE BOOL "SDL3: Build the SDL3_test library")
  set(SDL_EXAMPLES OFF CACHE BOOL "SDL3: Build the examples")

  fetchcontent_makeavailable(sdl3)

  set(SDL3_FOUND TRUE)
endif()
