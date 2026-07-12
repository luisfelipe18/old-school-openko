if(WIN32)
  find_package(MFC REQUIRED)

  # MFC (static)
  set(CMAKE_MFC_FLAG 1)

  add_executable(Option
    Option.cpp
    Option.h
    OptionDlg.cpp
    OptionDlg.h
    resource.h
    StdAfx.h)

  set_target_properties(Option PROPERTIES WIN32_EXECUTABLE TRUE)
  target_sources(Option PRIVATE Option.rc)

  # Setup PCH
  target_precompile_headers(Option PRIVATE StdAfx.h)
else()
  # POSIX port (docs/PORT_POSIX_PLAN.md): MFC has no POSIX equivalent, so
  # this is a from-scratch reimplementation on Dear ImGui over the SDL3 +
  # OpenGL stack the WarFare client already uses. OptionCore holds the
  # platform-neutral Option.ini/Server.Ini logic so it's unit-testable
  # without a display (see tests/Client/Option).
  find_package(OpenGL REQUIRED)

  add_executable(Option
    OptionCore.cpp
    OptionCore.h
    OptionMainSDL.cpp)

  target_link_libraries(Option PRIVATE
    Platform
    SDL3::SDL3
    imgui
    OpenGL::GL
    spdlog::spdlog
  )

  if(APPLE)
    # Apple deprecated the desktop OpenGL API in 10.14; the SDK marks every
    # gl* call with a [[deprecated]]-style attribute that -Werror promotes
    # to a hard error. OpenGL is still fully functional (just unmaintained)
    # and is what the WarFare client's GL backend also targets, so silence
    # the warning rather than the API itself.
    target_compile_definitions(Option PRIVATE GL_SILENCE_DEPRECATION)
  endif()
endif()

set_target_properties(Option PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
)

# Enable warnings as errors
if(OPENKO_COMPILE_WARNINGS_AS_ERROR)
  set_property(TARGET Option PROPERTY COMPILE_WARNING_AS_ERROR ON)
endif()

# Setup warning levels
target_compile_options(Option PRIVATE
  $<$<CXX_COMPILER_ID:MSVC>:/W3>
  $<$<CXX_COMPILER_ID:GNU>:-Wall>
  $<$<CXX_COMPILER_ID:Clang>:-Wall -Wextra>
)

if(MSVC)
  set_property(TARGET Option PROPERTY
    VS_DEBUGGER_WORKING_DIRECTORY "${OPENKO_CLIENT_DIR}"
  )
endif()

add_custom_command(TARGET Option POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E echo "$<TARGET_FILE:Option> has been built"
)
