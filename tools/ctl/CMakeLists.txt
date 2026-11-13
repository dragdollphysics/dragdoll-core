# dragdoll_ctl: sends one command to a running dragdoll_view over 127.0.0.1 and prints the JSON reply.
add_executable(dragdoll_ctl ctl.cpp)
target_link_libraries(dragdoll_ctl PRIVATE ws2_32)
dragdoll_set_compile_options(dragdoll_ctl)
