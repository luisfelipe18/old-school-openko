if(NOT WIN32)
  add_executable(KscViewer.Tests
    KscViewerCore_test.cpp
    "${CMAKE_SOURCE_DIR}/src/Client/KscViewer/KscViewerCore.cpp")

  set_target_properties(KscViewer.Tests PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
  )

  target_include_directories(KscViewer.Tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Client/KscViewer")

  # JpegFile (PUBLIC libjpeg) provides both the decoder used by the core and
  # the jpeglib.h the test uses to synthesize JPEG fixtures.
  target_link_libraries(KscViewer.Tests PRIVATE
    gtest
    gtest_main
    JpegFile)

  add_test(
    NAME KscViewerTests
    COMMAND KscViewer.Tests)
endif()
