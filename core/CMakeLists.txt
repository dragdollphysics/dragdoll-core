# Layer 0 — DragDoll core, built as dragdoll.dll with Jolt linked statically (ADR-0013, ADR-0015).

add_library(dragdoll SHARED
    src/capi_library.cpp
)
add_library(DragDoll::dragdoll ALIAS dragdoll)

target_include_directories(dragdoll
    PUBLIC  $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/capi/include>
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
)

target_compile_definitions(dragdoll PRIVATE DRAGDOLL_BUILDING)

# Jolt is an implementation detail: consumers only see the C header.
target_link_libraries(dragdoll PRIVATE dragdoll_jolt_config)

dragdoll_set_compile_options(dragdoll)
