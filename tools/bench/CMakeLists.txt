# dragdoll_bench: per-character CPU cost through the C ABI. The budget test runs in Release builds only.
add_executable(dragdoll_bench bench.cpp)
target_link_libraries(dragdoll_bench PRIVATE DragDoll::dragdoll)
dragdoll_set_compile_options(dragdoll_bench)

# Budget in microseconds per character per 120 Hz step (8 active ragdolls, single thread), per architecture. The
# reference machine (Ryzen 9 3900X) must stay inside it; x86 has its own, slightly higher number (portable SSE4.2,
# no AVX/FMA). Measured 2026-11-12: x64 ~70 us, x86 ~76-81 us per character per step.
# DRAGDOLL_BENCH_SLACK (an integer multiplier) widens it on noisy shared CI runners.
set(DRAGDOLL_BENCH_SLACK 1 CACHE STRING "Integer multiplier on the benchmark budgets (CI passes a larger value)")
if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_bench_base 120)
else()
    set(_bench_base 100)
endif()
math(EXPR _bench_budget "${_bench_base} * ${DRAGDOLL_BENCH_SLACK}")

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    add_test(NAME bench_budget
        COMMAND dragdoll_bench --characters 8 --threads 0 --seconds 4 --budget-us ${_bench_budget})
    set_tests_properties(bench_budget PROPERTIES FAIL_REGULAR_EXPRESSION "BUDGET EXCEEDED")
endif()
