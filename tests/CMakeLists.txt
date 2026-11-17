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
    unit/test_scenarios.cpp
    unit/test_character.cpp
    unit/test_vehicles.cpp
    unit/test_muscles.cpp
    unit/test_pose.cpp
    unit/test_activate.cpp
    unit/test_skeleton_map.cpp
    unit/test_failed.cpp
    unit/test_balance.cpp
    unit/test_live_tap.cpp
    unit/test_kinematic.cpp
    unit/test_perception.cpp
    unit/test_behaviour.cpp
    unit/test_aim.cpp
    unit/test_senses.cpp
    unit/test_impact_report.cpp
    unit/test_size_scale.cpp
    unit/test_memory.cpp
    unit/test_recording.cpp
)
target_link_libraries(dragdoll_tests PRIVATE DragDoll::dragdoll dragdoll_scenarios doctest::doctest)
if(WIN32)
    target_link_libraries(dragdoll_tests PRIVATE ws2_32) # Live tap test client
endif()
dragdoll_set_compile_options(dragdoll_tests)

add_test(NAME unit COMMAND dragdoll_tests)
