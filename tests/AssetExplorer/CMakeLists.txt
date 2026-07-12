if(NOT WIN32)
  add_executable(AssetExplorer.Tests
    AssetType_test.cpp
    AssetIndex_test.cpp
    "${CMAKE_SOURCE_DIR}/src/Client/AssetExplorer/AssetType.cpp"
    "${CMAKE_SOURCE_DIR}/src/Client/AssetExplorer/AssetIndex.cpp")

  set_target_properties(AssetExplorer.Tests PROPERTIES
    CXX_SCAN_FOR_MODULES OFF
  )

  target_include_directories(AssetExplorer.Tests PRIVATE
    "${CMAKE_SOURCE_DIR}/src"
    "${CMAKE_SOURCE_DIR}/src/Client/AssetExplorer")

  target_link_libraries(AssetExplorer.Tests PRIVATE
    gtest
    gtest_main)

  add_test(
    NAME AssetExplorerTests
    COMMAND AssetExplorer.Tests)
endif()
