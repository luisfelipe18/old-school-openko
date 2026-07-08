add_executable(Platform.Tests
  PlatformCrypto_test.cpp
  PlatformEncoding_test.cpp
  PlatformTime_test.cpp)

set_target_properties(Platform.Tests PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

target_link_libraries(Platform.Tests PRIVATE
  gtest
  gtest_main
  Platform)

add_test(
  NAME PlatformTests
  COMMAND Platform.Tests)
