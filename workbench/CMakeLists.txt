# dragdoll_view — live scenario viewer (raylib). Seed of the workbench; replaced/extended by the
# Dawn + ImGui workbench in Phase 2 if raylib turns out to be insufficient.
include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG        6.2
    GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(dragdoll_view main.cpp)
target_link_libraries(dragdoll_view PRIVATE dragdoll_scenarios raylib)
if(MSVC)
    # Dev tool: don't let third-party header warnings fail the build.
    target_compile_options(dragdoll_view PRIVATE /W3 /utf-8)
endif()
