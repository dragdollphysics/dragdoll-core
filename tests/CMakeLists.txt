include(FetchContent)

# ---------------------------------------------------------------------------
# Smoke test written in plain C: proves dragdoll.h is C-compatible and the DLL loads,
# initialises Jolt and shuts down cleanly on this architecture.
# ---------------------------------------------------------------------------
add_executable(dragdoll_smoke smoke/smoke.c)
target_link_libraries(dragdoll_smoke PRIVATE DragDoll::dragdoll)
if(MSVC)
    target_compile_options(dragdoll_smoke PRIVATE /W4 /WX)
endif()
add_test(NAME smoke COMMAND dragdoll_smoke)

# ---------------------------------------------------------------------------
# C++ unit/scenario tests against the public C ABI (doctest, pinned).
# ---------------------------------------------------------------------------
FetchContent_Declare(
    doctest
    GIT_REPOSITORY https://github.com/doctest/doctest.git
    GIT_TAG        v2.5.3
    GIT_SHALLOW    TRUE
)
set(DOCTEST_NO_INSTALL ON)
FetchContent_MakeAvailable(doctest)

add_executable(dragdoll_tests
    unit/main.cpp
    unit/test_world.cpp
)
target_link_libraries(dragdoll_tests PRIVATE DragDoll::dragdoll doctest::doctest)
dragdoll_set_compile_options(dragdoll_tests)

add_test(NAME unit COMMAND dragdoll_tests)
