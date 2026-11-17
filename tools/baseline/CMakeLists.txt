# dragdoll_baseline (task 4.3): the passive baseline - limp pedestrians x vehicle packs x speeds -> CSV + summary.
add_executable(dragdoll_baseline baseline.cpp)
target_link_libraries(dragdoll_baseline PRIVATE DragDoll::dragdoll dragdoll_scenarios)
dragdoll_set_compile_options(dragdoll_baseline)
