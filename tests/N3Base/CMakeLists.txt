add_executable(N3Base.Tests
  DFontFT_test.cpp
  RHIBuffers_test.cpp
  RHIDeviceNull_test.cpp
  RHITexture_test.cpp
  RHIStateKey_test.cpp
  UIEditTextInput_test.cpp
  VMeshHeadless_test.cpp)

set_target_properties(N3Base.Tests PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

target_link_libraries(N3Base.Tests PRIVATE
  gtest
  gtest_main
  N3Base_client)

add_test(
  NAME N3BaseTests
  COMMAND N3Base.Tests)
