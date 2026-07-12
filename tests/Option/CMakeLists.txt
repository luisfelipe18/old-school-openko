if(NOT WIN32)
  add_executable(Option.Tests
    OptionCore_test.cpp
    "${CMAKE_SOURCE_DIR}/src/Client/Option/OptionCore.cpp")

  set_target_properties(Option.Tests PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
  )

  target_include_directories(Option.Tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src")

  target_link_libraries(Option.Tests PRIVATE
    gtest
    gtest_main
    Platform)

  add_test(
    NAME OptionTests
    COMMAND Option.Tests)
endif()
