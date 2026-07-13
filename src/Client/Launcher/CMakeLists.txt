if(WIN32)
  find_package(MFC REQUIRED)

  # MFC (static)
  set(CMAKE_MFC_FLAG 1)

  add_executable(Launcher
    APISocket.cpp
    APISocket.h
    BackgroundUtil.cpp
    BackgroundUtil.h
    BB_CircularBuffer.cpp
    BB_CircularBuffer.h
    Define.h
    Launcher.cpp
    Launcher.h
    LauncherDlg.cpp
    LauncherDlg.h
    MacProgressCtrl.cpp
    MacProgressCtrl.h
    PacketDef.h
    resource.h
    StdAfx.h

    # Resources
    res/Bkg.bmp
    res/Btn_Connect.bmp
    res/Btn_cancel.bmp
    res/Btn_exit.bmp
    res/Btn_join.bmp
    res/Edit_PW.bmp
    res/Edit_id.bmp)

  set_target_properties(Launcher PROPERTIES WIN32_EXECUTABLE TRUE)
  target_sources(Launcher PRIVATE
    Launcher.rc
    res/Launcher.rc2
    res/Launcher.ico)

  # Setup PCH
  target_precompile_headers(Launcher PRIVATE StdAfx.h)

  # Setup dependencies
  target_link_libraries(Launcher PRIVATE
    ZipArchive
    wininet.lib
  )

  # Setup preprocessor defines
  target_compile_definitions(Launcher PRIVATE
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS)
else()
  # POSIX port (docs/PORT_POSIX_PLAN.md, F10): MFC has no POSIX equivalent.
  # LauncherCore holds the platform-neutral packet framing/parsing and
  # Server.Ini reading; LauncherPatch holds the modern patch-download logic
  # (HTTP[S] over Asio + miniz ZIP extraction, replacing the original's
  # FTP + Windows-only ZipArchive). Both are unit-testable without a display
  # (see tests/Launcher).
  find_package(OpenGL REQUIRED)
  require_package(Miniz)

  add_executable(Launcher
    LauncherCore.cpp
    LauncherCore.h
    LauncherPatch.cpp
    LauncherPatch.h
    LauncherMainSDL.cpp)

  target_link_libraries(Launcher PRIVATE
    Platform
    SDL3::SDL3
    imgui
    OpenGL::GL
    spdlog::spdlog
    asio
    shared
    miniz
  )

  if(APPLE)
    # Apple deprecated the desktop OpenGL API in 10.14; see Option's
    # CMakeLists.txt for the full rationale (same fix, same tradeoff).
    target_compile_definitions(Launcher PRIVATE GL_SILENCE_DEPRECATION)
  endif()

  # TLS for https:// patch URLs is opt-in on the presence of OpenSSL (always
  # available on Linux; `brew install openssl` on macOS). Plain http:// works
  # without it. On macOS point CMake at the Homebrew copy with
  # -DOPENSSL_ROOT_DIR=$(brew --prefix openssl) if find_package misses it.
  find_package(OpenSSL)
  if(OpenSSL_FOUND)
    target_link_libraries(Launcher PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(Launcher PRIVATE LAUNCHER_HAVE_TLS)
  else()
    message(STATUS "OpenKO: OpenSSL not found - Launcher patch downloads will "
                   "support http:// only (install OpenSSL for https://)")
  endif()

  find_package(Threads REQUIRED)
  target_link_libraries(Launcher PRIVATE Threads::Threads)

  # Stage the original Launcher.ico + Bkg.bmp (the classic launcher's
  # background artwork) next to the built binary so LauncherMainSDL's
  # runtime lookups find them without an install step.
  add_custom_command(TARGET Launcher POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/res/Launcher.ico"
      "$<TARGET_FILE_DIR:Launcher>/Launcher.ico"
    COMMAND "${CMAKE_COMMAND}" -E copy_if_different
      "${CMAKE_CURRENT_SOURCE_DIR}/res/Bkg.bmp"
      "$<TARGET_FILE_DIR:Launcher>/Bkg.bmp"
    COMMENT "Staging Launcher.ico + Bkg.bmp next to the Launcher binary")
endif()

set_target_properties(Launcher PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

# Enable warnings as errors
if(OPENKO_COMPILE_WARNINGS_AS_ERROR)
  set_property(TARGET Launcher PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# Setup warning levels
target_compile_options(Launcher PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W3>
  $<$<CXX_COMPILER_ID:GNU>:-Wall>
  $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
)

if(MSVC)
  set_property(TARGET Launcher PROPERTY
    VS_DEBUGGER_WORKING_DIRECTORY "${OPENKO_CLIENT_DIR}"
  )
endif()

add_custom_command(TARGET Launcher POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E echo "$<TARGET_FILE:Launcher> has been built"
)

# Ship next to the other client components (docs/PORT_POSIX_PLAN.md, Hito E).
if(APPLE)
  set_target_properties(Launcher PROPERTIES INSTALL_RPATH "@executable_path")
elseif(NOT WIN32)
  set_target_properties(Launcher PROPERTIES INSTALL_RPATH "$ORIGIN")
endif()
install(TARGETS Launcher RUNTIME DESTINATION "${OPENKO_CLIENT_INSTALL_DIR}")
install(FILES
  "${CMAKE_CURRENT_SOURCE_DIR}/res/Launcher.ico"
  "${CMAKE_CURRENT_SOURCE_DIR}/res/Bkg.bmp"
  DESTINATION "${OPENKO_CLIENT_INSTALL_DIR}" OPTIONAL)
