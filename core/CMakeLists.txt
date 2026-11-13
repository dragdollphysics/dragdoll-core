# Layer 0 — DragDoll core, built as dragdoll.dll with Jolt linked statically (ADR-0013, ADR-0015).

include(FetchContent)

# JSON parsing for rigs (header-only, pinned release archive).
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/releases/download/v3.12.0/json.tar.xz
)
set(JSON_Install OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(nlohmann_json)

# Built-in rigs: packs/rigs/*.json compiled into the library (re-generated when the JSON changes).
set(DD_RIG_FILE ${PROJECT_SOURCE_DIR}/packs/rigs/humanoid16.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DD_RIG_FILE})
file(READ ${DD_RIG_FILE} DD_RIG_HUMANOID16)
set(DD_SKELETON_SA_PED_FILE ${PROJECT_SOURCE_DIR}/packs/skeletons/sa_ped.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DD_SKELETON_SA_PED_FILE})
file(READ ${DD_SKELETON_SA_PED_FILE} DD_SKELETON_SA_PED)
configure_file(src/builtin_rigs.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/builtin_rigs.cpp @ONLY)

add_library(dragdoll SHARED
    src/capi_guard.hpp
    src/capi_world_handle.hpp
    src/capi_library.cpp
    src/memory_stats.hpp
    src/memory_stats.cpp
    src/capi_world.cpp
    src/capi_character.cpp
    src/capi_debug_draw.cpp
    src/capi_replay.cpp
    src/recording.hpp
    src/recording.cpp
    src/world.hpp
    src/world.cpp
    src/rig.hpp
    src/rig.cpp
    src/character.hpp
    src/character.cpp
    src/muscles.cpp
    src/pose.cpp
    src/skeleton_map.cpp
    src/capi_skeleton.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/builtin_rigs.cpp
)
add_library(DragDoll::dragdoll ALIAS dragdoll)

target_include_directories(dragdoll
    PUBLIC  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/capi/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(dragdoll PRIVATE DRAGDOLL_BUILDING)

# Jolt and nlohmann/json are implementation details: consumers only see the C header.
target_link_libraries(dragdoll PRIVATE dragdoll_jolt_config nlohmann_json::nlohmann_json)

dragdoll_set_compile_options(dragdoll)
