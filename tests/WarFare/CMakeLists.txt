add_executable(WarFareNet.Tests
  APISocket_test.cpp)

set_target_properties(WarFareNet.Tests PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

target_include_directories(WarFareNet.Tests PRIVATE
  "${CMAKE_SOURCE_DIR}/src/Client/WarFare")

target_link_libraries(WarFareNet.Tests PRIVATE
  gtest
  gtest_main
  asio
  WarFare.Core)

add_test(
  NAME WarFareNetTests
  COMMAND WarFareNet.Tests)

if(NOT WIN32)
  # POSIX client-layer tests (cursor decoding + SDL scancode mapping).
  add_executable(WarFareClient.Tests
    CursorDecoder_test.cpp
    ScancodeMap_test.cpp
    TextResource_test.cpp)

  set_target_properties(WarFareClient.Tests PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
  )

  target_compile_definitions(WarFareClient.Tests PRIVATE
    WARFARE_SOURCE_DIR="${CMAKE_SOURCE_DIR}/src/Client/WarFare")

  target_include_directories(WarFareClient.Tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src/Client/WarFare")

  target_link_libraries(WarFareClient.Tests PRIVATE
    gtest
    gtest_main
    SDL3::SDL3
    WarFare.Core)

  add_test(
    NAME WarFareClientTests
    COMMAND WarFareClient.Tests)
endif()
