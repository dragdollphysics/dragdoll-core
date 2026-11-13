# dragdoll_inspect: query .ddrec recordings (notes, per-body motion) through the public C ABI.
add_executable(dragdoll_inspect inspect.cpp)
target_link_libraries(dragdoll_inspect PRIVATE DragDoll::dragdoll)
dragdoll_set_compile_options(dragdoll_inspect)
