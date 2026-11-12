# Smoke test written in plain C: proves dragdoll.h is C-compatible and the DLL loads,
# initialises Jolt and shuts down cleanly on this architecture.
add_executable(dragdoll_smoke smoke/smoke.c)
target_link_libraries(dragdoll_smoke PRIVATE DragDoll::dragdoll)
if(MSVC)
    target_compile_options(dragdoll_smoke PRIVATE /W4 /WX)
endif()

add_test(NAME smoke COMMAND dragdoll_smoke)
