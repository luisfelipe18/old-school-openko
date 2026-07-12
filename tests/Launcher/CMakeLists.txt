if(NOT WIN32)
  add_executable(Launcher.Tests
    LauncherCore_test.cpp
    "${CMAKE_SOURCE_DIR}/src/Client/Launcher/LauncherCore.cpp")

  set_target_properties(Launcher.Tests PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
  )

  target_include_directories(Launcher.Tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src")

  target_link_libraries(Launcher.Tests PRIVATE
    gtest
    gtest_main
    Platform
    shared)

  add_test(
    NAME LauncherTests
    COMMAND Launcher.Tests)
endif()
