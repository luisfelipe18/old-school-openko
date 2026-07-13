if(NOT WIN32)
  require_package(Miniz)

  add_executable(Launcher.Tests
    LauncherCore_test.cpp
    LauncherPatch_test.cpp
    "${CMAKE_SOURCE_DIR}/src/Client/Launcher/LauncherCore.cpp"
    "${CMAKE_SOURCE_DIR}/src/Client/Launcher/LauncherPatch.cpp")

  set_target_properties(Launcher.Tests PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
  )

  target_include_directories(Launcher.Tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Client/Launcher")

  target_link_libraries(Launcher.Tests PRIVATE
    gtest
    gtest_main
    Platform
    shared
    asio
    miniz)

  find_package(OpenSSL)
  if(OpenSSL_FOUND)
    target_link_libraries(Launcher.Tests PRIVATE OpenSSL::SSL OpenSSL::Crypto)
    target_compile_definitions(Launcher.Tests PRIVATE LAUNCHER_HAVE_TLS)
  endif()

  find_package(Threads REQUIRED)
  target_link_libraries(Launcher.Tests PRIVATE Threads::Threads)

  add_test(
    NAME LauncherTests
    COMMAND Launcher.Tests)
endif()
