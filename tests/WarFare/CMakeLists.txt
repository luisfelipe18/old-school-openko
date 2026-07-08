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
