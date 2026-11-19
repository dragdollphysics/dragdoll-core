# dragdoll_crashlab: the crash sandbox swept over thousands of settings, each run judged by the analyzer -> CSV.
add_executable(dragdoll_crashlab crashlab.cpp)
target_link_libraries(dragdoll_crashlab PRIVATE DragDoll::dragdoll dragdoll_scenarios)
dragdoll_set_compile_options(dragdoll_crashlab)
