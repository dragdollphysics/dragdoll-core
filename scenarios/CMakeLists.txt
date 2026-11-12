# Shared scenario library: built only on the public C ABI, used by the unit tests (numbers)
# and by the live viewer (looks). One definition, two consumers.
add_library(dragdoll_scenarios STATIC
    include/dragdoll_scenarios/scenario.hpp
    src/scenarios.cpp
)
target_include_directories(dragdoll_scenarios PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dragdoll_scenarios PUBLIC DragDoll::dragdoll)
dragdoll_set_compile_options(dragdoll_scenarios)
