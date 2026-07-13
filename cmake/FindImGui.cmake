# Get Dear ImGui (docs/PORT_POSIX_PLAN.md, client tool ports: Option/
# KscViewer/Launcher on POSIX).
#
# Dear ImGui doesn't publish a library CMakeLists.txt upstream (it's meant to
# be dropped into the consumer's build), so this fetches the source and
# declares the `imgui` static library target ourselves: the core library plus
# the SDL3 + OpenGL3 backends, matching the windowing/GL stack the WarFare
# POSIX port already uses (SDL3 window + GL 3.3 core context).

fetchcontent_declare(
  imgui
  GIT_REPOSITORY        "https://github.com/ocornut/imgui.git"
  GIT_TAG               "v1.92.8"
  GIT_PROGRESS          ON
  GIT_SHALLOW           ON
  EXCLUDE_FROM_ALL
)

fetchcontent_makeavailable(imgui)

add_library(imgui STATIC
  "${imgui_SOURCE_DIR}/imgui.cpp"
  "${imgui_SOURCE_DIR}/imgui_draw.cpp"
  "${imgui_SOURCE_DIR}/imgui_tables.cpp"
  "${imgui_SOURCE_DIR}/imgui_widgets.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_sdl3.cpp"
  "${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp"
)

target_include_directories(imgui SYSTEM PUBLIC
  "${imgui_SOURCE_DIR}"
  "${imgui_SOURCE_DIR}/backends"
)

target_link_libraries(imgui PUBLIC SDL3::SDL3)

set_target_properties(imgui PROPERTIES
  CXX_SCAN_FOR_MODULES OFF
  POSITION_INDEPENDENT_CODE ON
)

set(ImGui_FOUND TRUE)
