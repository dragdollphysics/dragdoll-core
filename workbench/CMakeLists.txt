# dragdoll_view — live scenario viewer (raylib + Dear ImGui via rlImGui, ADR-0021).
include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

# Dear ImGui (MIT) and rlImGui (zlib): no CMake of their own, so only fetch the sources.
FetchContent_Declare(
    imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        v1.92.9
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  no-cmake
)
FetchContent_Declare(
    rlimgui
    GIT_REPOSITORY https://github.com/raylib-extras/rlImGui.git
    GIT_TAG        5c17e70e98ae9f50ca94bdcad0e3b3536b46a203
    SOURCE_SUBDIR  no-cmake
)
FetchContent_MakeAvailable(imgui rlimgui)

add_library(dragdoll_imgui STATIC
    ${imgui_SOURCE_DIR}/imgui.cpp
    ${imgui_SOURCE_DIR}/imgui_draw.cpp
    ${imgui_SOURCE_DIR}/imgui_tables.cpp
    ${imgui_SOURCE_DIR}/imgui_widgets.cpp
    ${rlimgui_SOURCE_DIR}/rlImGui.cpp
)
target_include_directories(dragdoll_imgui PUBLIC ${imgui_SOURCE_DIR} ${rlimgui_SOURCE_DIR})
target_compile_definitions(dragdoll_imgui PUBLIC NO_FONT_AWESOME)
target_link_libraries(dragdoll_imgui PUBLIC raylib)
if(MSVC)
    target_compile_options(dragdoll_imgui PRIVATE /W0 /utf-8)
endif()

add_executable(dragdoll_view main.cpp)
target_link_libraries(dragdoll_view PRIVATE dragdoll_scenarios raylib dragdoll_imgui)
if(MSVC)
    # Dev tool: don't let third-party header warnings fail the build.
    target_compile_options(dragdoll_view PRIVATE /W3 /utf-8)
endif()
