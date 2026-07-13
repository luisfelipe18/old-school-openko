if(WIN32)
  find_package(MFC REQUIRED)

  # MFC (static)
  set(CMAKE_MFC_FLAG 1)

  add_executable(KscViewer
    KscViewer.cpp
    KscViewer.h
    KscViewerDoc.cpp
    KscViewerDoc.h
    KscViewerView.cpp
    KscViewerView.h
    MainFrm.cpp
    MainFrm.h
    N3JpegFile.cpp
    N3JpegFile.h
    Resource.h
    StdAfx.h)

  set_target_properties(KscViewer PROPERTIES WIN32_EXECUTABLE TRUE)
  target_sources(KscViewer PRIVATE KscViewer.rc)

  # Setup PCH
  target_precompile_headers(KscViewer PRIVATE StdAfx.h)

  # Setup dependencies
  target_link_libraries(KscViewer PRIVATE
    JpegFile
  )
else()
  # POSIX port (docs/PORT_POSIX_PLAN.md, F10): the original is an MFC
  # doc/view app; MFC has no POSIX equivalent, so this is a from-scratch
  # reimplementation on Dear ImGui over the SDL3 + OpenGL stack the other
  # tool ports use. KscViewerCore holds the platform-neutral decrypt/decode/
  # export logic so it's unit-testable without a display (tests/KscViewer).
  find_package(OpenGL REQUIRED)

  add_executable(KscViewer
    KscViewerCore.cpp
    KscViewerCore.h
    KscViewerMainSDL.cpp)

  target_link_libraries(KscViewer PRIVATE
    JpegFile
    Platform
    SDL3::SDL3
    imgui
    OpenGL::GL
    spdlog::spdlog
  )

  if(APPLE)
    # Apple deprecated the desktop OpenGL API in 10.14; see Option's
    # CMakeLists.txt for the full rationale (same fix, same tradeoff).
    target_compile_definitions(KscViewer PRIVATE GL_SILENCE_DEPRECATION)
  endif()

  # Stage the original KscViewer.ico next to the binary so the runtime
  # window-icon lookup finds it, matching the Option/Launcher ports.
  add_custom_command(TARGET KscViewer POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/res/KscViewer.ico"
      "$<TARGET_FILE_DIR:KscViewer>/KscViewer.ico"
    COMMENT "Staging KscViewer.ico next to the KscViewer binary")
endif()

set_target_properties(KscViewer PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

# Enable warnings as errors
if(OPENKO_COMPILE_WARNINGS_AS_ERROR)
  set_property(TARGET KscViewer PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# Setup warning levels
target_compile_options(KscViewer PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W3>
  $<$<CXX_COMPILER_ID:GNU>:-Wall>
  $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
)

if(MSVC)
  set_property(TARGET KscViewer PROPERTY
    VS_DEBUGGER_WORKING_DIRECTORY "${OPENKO_CLIENT_DIR}"
  )
endif()

add_custom_command(TARGET KscViewer POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E echo "$<TARGET_FILE:KscViewer> has been built"
)

# Ship next to the other client components (docs/PORT_POSIX_PLAN.md, Hito E).
# KscViewer links the vendored libjpeg, which ships next to the binaries; the
# install rpath makes it findable from the package without LD_LIBRARY_PATH.
if(APPLE)
  set_target_properties(KscViewer PROPERTIES INSTALL_RPATH "@executable_path")
elseif(NOT WIN32)
  set_target_properties(KscViewer PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
install(TARGETS KscViewer RUNTIME DESTINATION "${OPENKO_CLIENT_INSTALL_DIR}")
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/res/KscViewer.ico"
  DESTINATION "${OPENKO_CLIENT_INSTALL_DIR}" OPTIONAL)
