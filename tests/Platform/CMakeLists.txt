add_executable(Platform.Tests
  IconDecoder_test.cpp
  PlatformCrypto_test.cpp
  PlatformEncoding_test.cpp
  PlatformIni_test.cpp
  PlatformPaths_test.cpp
  PlatformTime_test.cpp
  ProcessLaunch_test.cpp)

set_target_properties(Platform.Tests PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

target_compile_definitions(Platform.Tests PRIVATE
  # Real .ico files shipped with the client tools, used to test IconDecoder
  # against actual data instead of only hand-built fixtures (same spirit as
  # CursorDecoder_test.cpp's WARFARE_SOURCE_DIR).
  OPTION_RES_DIR="${CMAKE_SOURCE_DIR}/src/Client/Option/res")

target_link_libraries(Platform.Tests PRIVATE
  gtest
  gtest_main
  Platform)

add_test(
  NAME PlatformTests
  COMMAND Platform.Tests)
