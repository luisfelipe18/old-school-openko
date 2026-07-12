# Asset Explorer (docs/ASSET_EXPLORER_PLAN.md). A modern SDL3 + Dear ImGui tool
# to browse and preview every N3 asset type. POSIX-only: it is a new tool with
# no MFC original, so there is no Windows arm to preserve (the legacy per-type
# MFC tools under src/Tools remain for Windows).
if(NOT WIN32)
  find_package(OpenGL REQUIRED)

  add_executable(AssetExplorer
    AssetType.cpp
    AssetType.h
    AssetIndex.cpp
    AssetIndex.h
    OrbitCamera.cpp
    OrbitCamera.h
    AnimationPlayer.cpp
    AnimationPlayer.h
    AssetExplorerMainSDL.cpp
    # Reuse KscViewer's platform-neutral .ksc decrypt/decode/export core for the
    # encrypted-texture preview (M2), rather than duplicating the cipher.
    "${CMAKE_SOURCE_DIR}/src/Client/KscViewer/KscViewerCore.cpp")

  # WarFare.Core carries the engine asset loaders and the GL RHI backend
  # (RHIDeviceGL) the preview viewport renders through; N3Base_client comes
  # transitively. The tool needs the WarFare source dir on its include path to
  # reach RHIDeviceGL.h.
  # SYSTEM so the engine headers' own warnings (e.g. -Wdeprecated-copy in the
  # math types) don't fail the tool's -Werror build; the tool's own headers are
  # found relative to the sources, so their warnings are still active.
  target_include_directories(AssetExplorer SYSTEM PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Client/WarFare"
    "${CMAKE_SOURCE_DIR}/src/Client/KscViewer")

  target_link_libraries(AssetExplorer PRIVATE
    WarFare.Core
    JpegFile
    Platform
    SDL3::SDL3
    imgui
    OpenGL::GL
    spdlog::spdlog)

  if(APPLE)
    # Apple deprecated the desktop OpenGL API in 10.14; see Option's
    # CMakeLists.txt for the full rationale (same fix, same tradeoff).
    target_compile_definitions(AssetExplorer PRIVATE GL_SILENCE_DEPRECATION)
  endif()

  set_target_properties(AssetExplorer PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
  )

  # Enable warnings as errors
  if(OPENKO_COMPILE_WARNINGS_AS_ERROR)
    set_property(TARGET AssetExplorer PROPERTY COMPILE_WARNING_AS_ERROR ON)
  endif()

  # Setup warning levels
  target_compile_options(AssetExplorer PRIVATE
    $<$<CXX_COMPILER_ID:GNU>:-Wall>
    $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
  )

  add_custom_command(TARGET AssetExplorer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E echo "$<TARGET_FILE:AssetExplorer> has been built"
  )
endif()
