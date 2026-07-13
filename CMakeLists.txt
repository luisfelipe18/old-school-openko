cmake_minimum_required(VERSION 3.28.3)

project(OpenKO
  VERSION 0.0.1
)

find_package(Git REQUIRED)

# Search in the "cmake" directory for additional CMake modules.
set(OPENKO_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${OPENKO_MODULE_PATH}")

include(FetchContent)
include(RequirePackage)

# Set minimum standard to C++20 (unless otherwise specified)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# We don't use modules, so there's no reason to scan
set(CXX_SCAN_FOR_MODULES OFF)

if(MSVC)
  # Statically link to the MSVC runtime
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  
  # Ensure consistent charset builds across projects
  add_compile_options(/utf-8)
endif()

if(MINGW)
  # Statically link (everything)
  add_link_options(-static)
endif()

# Config
option(OPENKO_BUILD_CLIENT "Build the client projects - MSVC only (default on)" ON)
option(OPENKO_BUILD_CLIENT_TOOLS "Build the client tool projects - MSVC only (default on)" ON)
option(OPENKO_BUILD_TOOLS "Build the development tool projects - MSVC only (default on)" ON)
option(OPENKO_BUILD_SERVERS "Build the server projects (default on)" ON)
option(OPENKO_BUILD_TESTS "Build the test projects (default on)" ON)
option(OPENKO_COMPILE_WARNINGS_AS_ERROR "Enable treating warnings as errors (default on)" ON)
option(OPENKO_FETCH_CLIENT_ASSETS "Enable fetching client assets (default on)" ON)
option(OPENKO_CLIENT_POSIX_EXPERIMENTAL "Experimental: allow enabling the client build on POSIX platforms (macOS/Linux) while the port is in progress - see docs/PORT_POSIX_PLAN.md (default off)" OFF)
option(OPENKO_CLIENT_TOOLS_POSIX_EXPERIMENTAL "Experimental: allow enabling the client tool ports (Option, KscViewer, Launcher - reimplemented on Dear ImGui instead of MFC) on POSIX platforms while they're being ported - see docs/PORT_POSIX_PLAN.md (default off)" OFF)

# Define the bin directory; defaults to repo/bin
set(OPENKO_BIN_DIR "${CMAKE_BINARY_DIR}/bin" CACHE PATH "Directory for final binaries")

if(WIN32)
  # Set the minimum Windows version
  set(_WIN32_WINNT "0x0A00" CACHE STRING "Minimum Windows version (default 0x0A00 - Windows 10)")
  add_definitions(-D_WIN32_WINNT=${_WIN32_WINNT})

  if(MINGW)
    set(OPENKO_BUILD_CLIENT_TOOLS OFF)
    set(OPENKO_BUILD_TOOLS OFF)
  endif()
else()
  # Client & tool projects are currently only supported on Windows.
  # The client is being ported to POSIX platforms (see docs/PORT_POSIX_PLAN.md);
  # configuring it there can be opted into with -DOPENKO_CLIENT_POSIX_EXPERIMENTAL=ON.
  # NOTE: only a subset of the client targets currently builds on these platforms.
  if(OPENKO_CLIENT_POSIX_EXPERIMENTAL)
    message(STATUS "OpenKO: OPENKO_CLIENT_POSIX_EXPERIMENTAL is ON - keeping OPENKO_BUILD_CLIENT=${OPENKO_BUILD_CLIENT} on this platform (work-in-progress port)")
  else()
    set(OPENKO_BUILD_CLIENT OFF)
  endif()
  if(OPENKO_CLIENT_TOOLS_POSIX_EXPERIMENTAL)
    message(STATUS "OpenKO: OPENKO_CLIENT_TOOLS_POSIX_EXPERIMENTAL is ON - keeping OPENKO_BUILD_CLIENT_TOOLS=${OPENKO_BUILD_CLIENT_TOOLS} on this platform (work-in-progress port)")
  else()
    set(OPENKO_BUILD_CLIENT_TOOLS OFF)
  endif()
  set(OPENKO_BUILD_TOOLS OFF)
endif()

message(STATUS "OpenKO feature: OPENKO_BUILD_CLIENT - ${OPENKO_BUILD_CLIENT}")
message(STATUS "OpenKO feature: OPENKO_BUILD_CLIENT_TOOLS - ${OPENKO_BUILD_CLIENT_TOOLS}")
message(STATUS "OpenKO feature: OPENKO_BUILD_TOOLS - ${OPENKO_BUILD_TOOLS}")
message(STATUS "OpenKO feature: OPENKO_BUILD_SERVERS - ${OPENKO_BUILD_SERVERS}")
message(STATUS "OpenKO feature: OPENKO_BUILD_TESTS - ${OPENKO_BUILD_TESTS}")
message(STATUS "OpenKO feature: OPENKO_COMPILE_WARNINGS_AS_ERROR - ${OPENKO_COMPILE_WARNINGS_AS_ERROR}")
message(STATUS "OpenKO feature: OPENKO_FETCH_CLIENT_ASSETS - ${OPENKO_FETCH_CLIENT_ASSETS}")
message(STATUS "OpenKO feature: OPENKO_CLIENT_POSIX_EXPERIMENTAL - ${OPENKO_CLIENT_POSIX_EXPERIMENTAL}")
message(STATUS "OpenKO feature: OPENKO_CLIENT_TOOLS_POSIX_EXPERIMENTAL - ${OPENKO_CLIENT_TOOLS_POSIX_EXPERIMENTAL}")
message(STATUS "OpenKO feature: OPENKO_BIN_DIR - ${OPENKO_BIN_DIR}")

if(WIN32)
  message(STATUS "OpenKO feature: _WIN32_WINNT - ${_WIN32_WINNT}")
endif()

get_property(is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)

# Multiconfig generators don't require a build type at config time.
# They generate multiple configurations.
# The output directory will have the build type auto-appended.
if(is_multi_config)
  # Set output directory
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OPENKO_BIN_DIR}")
else()
  # Default build type to Debug
  if(NOT CMAKE_BUILD_TYPE)
    message(STATUS "OpenKO: Defaulting build type to 'Debug' as none was specified via -DCMAKE_BUILD_TYPE (expected build types: Debug, Release, RelWithDebugInfo, MinSizeRel)")

    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
  endif()

  # Set output directory
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${OPENKO_BIN_DIR}/${CMAKE_BUILD_TYPE}")
endif()

# Ensure consistent debug flags
# These are typically used by MSVC, so we expect them to exist
add_compile_definitions("$<$<CONFIG:Debug>:DEBUG;_DEBUG>")

set(OPENKO_CLIENT_DIR "${CMAKE_BINARY_DIR}/ClientData" CACHE STRING "Client path")
set(OPENKO_SERVER_MAP_SRC_DIR "${CMAKE_SOURCE_DIR}/assets/Server/MAP")
set(OPENKO_SERVER_MAP_DST_DIR "${OPENKO_BIN_DIR}/MAP")
set(OPENKO_SERVER_QUESTS_SRC_DIR "${CMAKE_SOURCE_DIR}/assets/Server/QUESTS")
set(OPENKO_SERVER_QUESTS_DST_DIR "${OPENKO_BIN_DIR}/QUESTS")

# Ensure the client directory exists.
file(MAKE_DIRECTORY "${OPENKO_CLIENT_DIR}")

# Generate .gitignore in build directory
if(NOT PROJECT_SOURCE_DIR STREQUAL PROJECT_BINARY_DIR)
  file(WRITE "${PROJECT_BINARY_DIR}/.gitignore" "*")
endif()

# Add dependencies
set(CMAKE_FOLDER "_deps")
require_package(Asio)
require_package(BoostSubset)
require_package(Spdlog)

include_directories(
  "${CMAKE_SOURCE_DIR}" # repo root, for <shared/...>
)

set(CMAKE_FOLDER) # reset so shared is placed in the root
add_subdirectory(src/shared)
add_subdirectory(src/FileIO)
add_subdirectory(src/MathUtils)
add_subdirectory(src/Platform)

if(OPENKO_BUILD_CLIENT OR OPENKO_BUILD_TOOLS)
  # Dependencies
  if(WIN32)
    # The DirectX 9 SDK only applies to Windows; the POSIX client port
    # renders through the RHI backends instead (see docs/PORT_POSIX_PLAN.md).
    set(CMAKE_FOLDER "N3Base/_deps")
    require_config_package(dx9sdk)
  endif()

  set(CMAKE_FOLDER "N3Base/_deps/mpg123")
  require_package(Mpg123)

  set(CMAKE_FOLDER "N3Base/_deps/openal-soft")
  require_package(OpenALSoft)

  if(NOT WIN32)
    # POSIX port (T7.1): FreeType rasterizes the DFont glyph atlas; Windows
    # keeps the GDI text path.
    set(CMAKE_FOLDER "N3Base/_deps/freetype")
    require_package(FreetypeFont)
  endif()

  set(CMAKE_FOLDER "N3Base")
  add_subdirectory(src/N3Base)
endif()

if(OPENKO_BUILD_CLIENT OR OPENKO_BUILD_CLIENT_TOOLS)
  set(CMAKE_FOLDER "Client/_deps")

  # We technically require this for the Launcher & Option as well as the client itself (WarFare),
  # so we'll pull this in here.
  require_config_package(client-assets)

  require_package(Jpeg)
  add_subdirectory(src/Client/JpegFile)
endif()

if(OPENKO_BUILD_CLIENT)
  message(STATUS "OpenKO: Preparing to configure client...")

  if(NOT WIN32)
    # POSIX port (experimental): SDL3 provides the windowing/input/GPU layer.
    # Prefers a system/package-manager SDL3; falls back to building from source.
    set(CMAKE_FOLDER "Client/_deps")
    require_package(SDL3)
  endif()

  set(CMAKE_FOLDER "Client")
  add_subdirectory(src/Client/WarFare)
endif()

if(OPENKO_BUILD_CLIENT_TOOLS)
  message(STATUS "OpenKO: Preparing to configure client tools...")

  if(NOT WIN32)
    # POSIX ports (experimental, docs/PORT_POSIX_PLAN.md): the MFC dialogs
    # are reimplemented on Dear ImGui, reusing the SDL3 windowing stack the
    # WarFare client already brought in.
    set(CMAKE_FOLDER "Client tools/_deps")
    require_package(SDL3)
    require_package(ImGui)
  endif()

  if(WIN32)
    # ZipArchive is MFC-only. Launcher's Windows build needs it for patch
    # extraction.
    set(CMAKE_FOLDER "Client tools/_deps")
    require_package(Zlib)
    add_subdirectory(src/Client/ZipArchive)
  endif()

  set(CMAKE_FOLDER "Client tools")
  add_subdirectory(src/Client/Option)
  add_subdirectory(src/Client/Launcher)
  add_subdirectory(src/Client/KscViewer)
  if(NOT WIN32)
    add_subdirectory(src/Client/AssetExplorer)
  endif()
endif()

if(OPENKO_BUILD_TOOLS)
  message(STATUS "OpenKO: Preparing to configure development tools...")

  set(CMAKE_FOLDER "Tools")
  add_subdirectory(src/Tools/N3CE)
  add_subdirectory(src/Tools/N3FXE)
  add_subdirectory(src/Tools/N3ME)
  add_subdirectory(src/Tools/N3TexViewer)
  add_subdirectory(src/Tools/N3Viewer)
  add_subdirectory(src/Tools/SkyViewer)
  add_subdirectory(src/Tools/TblEditor)
  add_subdirectory(src/Tools/UIE)
endif()

if(OPENKO_BUILD_SERVERS AND NOT WIN32)
  # The servers need a system ODBC library (unixODBC on Linux, unixodbc or
  # iODBC from Homebrew on macOS). Rather than letting the "all" build fail
  # at link time on client-only development machines, detect it up front
  # and skip the server projects with a clear notice.
  find_library(OPENKO_ODBC_LIBRARY NAMES odbc iodbc)
  if(NOT OPENKO_ODBC_LIBRARY)
    message(STATUS "OpenKO: no ODBC library found (unixODBC/iODBC) - skipping the server "
                   "projects. Install it (apt: unixodbc-dev / brew: unixodbc) and "
                   "reconfigure to build them.")
    set(OPENKO_BUILD_SERVERS OFF)
  endif()
endif()

if(OPENKO_BUILD_SERVERS)
  message(STATUS "OpenKO: Preparing to configure servers...")

  # Dependencies
  set(CMAKE_FOLDER "Server/_deps/argparse")
  require_package(Argparse)

  set(CMAKE_FOLDER "Server/_deps")
  require_package(Nanodbc)
  require_config_package(db-models)
  require_config_package(djb2)
  add_subdirectory(src/db-library)
  add_subdirectory(src/Server/shared-server)

  add_custom_target(copy-server-assets ALL
    COMMENT "Copying server assets"
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying server MAP dir to ${OPENKO_SERVER_MAP_DST_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${OPENKO_SERVER_MAP_SRC_DIR}" "${OPENKO_SERVER_MAP_DST_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo "Copying server QUESTS dir to ${OPENKO_SERVER_QUESTS_DST_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy_directory "${OPENKO_SERVER_QUESTS_SRC_DIR}" "${OPENKO_SERVER_QUESTS_DST_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E echo "Server assets copied."
  )

  # Servers
  set(CMAKE_FOLDER "Server")
  add_subdirectory(src/Server/AIServer)
  add_subdirectory(src/Server/Aujard)
  add_subdirectory(src/Server/Ebenezer)
  add_subdirectory(src/Server/ItemManager)
  add_subdirectory(src/Server/VersionManager)
endif()

if(OPENKO_BUILD_TESTS)
  include(CTest)

  set(CMAKE_FOLDER "Tests/_deps")
  require_package(Googletest)

  set(CMAKE_FOLDER "Tests")
  add_subdirectory(tests/FileIO)
  add_subdirectory(tests/MathUtils)
  add_subdirectory(tests/Platform)

  if(OPENKO_BUILD_CLIENT)
    add_subdirectory(tests/N3Base)
    add_subdirectory(tests/WarFare)
  endif()

  if(OPENKO_BUILD_CLIENT_TOOLS)
    add_subdirectory(tests/Option)
    add_subdirectory(tests/Launcher)
    add_subdirectory(tests/KscViewer)
    if(NOT WIN32)
      add_subdirectory(tests/AssetExplorer)
    endif()
  endif()

  if(OPENKO_BUILD_SERVERS)
    add_subdirectory(tests/Server/Ebenezer)
  endif()
endif()

# ---------------------------------------------------------------------------
# Distributable packaging (docs/PORT_POSIX_PLAN.md, Hito E).
#
# Produces a self-contained client artifact via `cpack`:
#   Linux -> OpenKO-Client-<ver>-Linux-<arch>.tar.gz  (binary + vendored .so +
#            cursors + .desktop, run bin/KnightOnLine)
#   macOS -> OpenKO-Client-<ver>-Darwin-<arch>.dmg    (KnightOnLine.app, drag to
#            /Applications; sign + notarize separately - see PACKAGING_README)
#
# Game DATA (Data/, UI/, Server.Ini) is intentionally NOT packaged: it is the
# proprietary Knight OnLine content the user supplies. `include(CPack)` must
# come after every install() rule, hence its place at the very end of the file.
# ---------------------------------------------------------------------------
if(NOT WIN32 AND OPENKO_BUILD_CLIENT)
  set(CPACK_PACKAGE_NAME               "OpenKO-Client")
  set(CPACK_PACKAGE_VENDOR             "OpenKO contributors")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "OpenKO WarFare client (Knight OnLine) for macOS/Linux")
  set(CPACK_PACKAGE_VERSION_MAJOR      "0")
  set(CPACK_PACKAGE_VERSION_MINOR      "1")
  set(CPACK_PACKAGE_VERSION_PATCH      "0")
  set(CPACK_PACKAGE_FILE_NAME
    "OpenKO-Client-${CPACK_PACKAGE_VERSION_MAJOR}.${CPACK_PACKAGE_VERSION_MINOR}.${CPACK_PACKAGE_VERSION_PATCH}-${CMAKE_SYSTEM_NAME}-${CMAKE_SYSTEM_PROCESSOR}")
  set(CPACK_VERBATIM_VARIABLES         ON)
  set(CPACK_STRIP_FILES                ON)

  if(APPLE)
    # A DMG holding KnightOnLine.app; fixup_bundle (WarFare/CMakeLists.txt) has
    # already made the .app self-contained at install time.
    set(CPACK_GENERATOR "DragNDrop")
  else()
    # A relocatable tar.gz rooted at the install prefix (bin/, share/).
    set(CPACK_GENERATOR "TGZ")
    set(CPACK_PACKAGING_INSTALL_PREFIX "/")
  endif()

  include(CPack)
endif()
