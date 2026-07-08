add_library(Platform STATIC
  D3D9Types.h
  PlatformEncoding.cpp
  PlatformEncoding.h
  PlatformString.h
  PlatformTime.h
  PlatformTypes.h
)

set_target_properties(Platform PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

# Allow #include <Platform/...> from anywhere that links against us.
target_include_directories(Platform PUBLIC
  "${CMAKE_SOURCE_DIR}/src"
)

# The POSIX encoding implementation uses iconv. It is part of libc on
# glibc/musl systems and a separate library (libiconv) on macOS/BSDs;
# CMake's standard Iconv module sorts that out.
if(NOT WIN32)
  find_package(Iconv REQUIRED)
  target_link_libraries(Platform PRIVATE Iconv::Iconv)
endif()

# Enable warnings as errors
if(OPENKO_COMPILE_WARNINGS_AS_ERROR)
  set_property(TARGET Platform PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# Setup warning levels
target_compile_options(Platform PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W3>
  $<$<CXX_COMPILER_ID:GNU>:-Wall>
  $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
)
