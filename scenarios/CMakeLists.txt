# Shared scenario library: built only on the public C ABI, used by the unit tests (numbers)
# and by the live viewer (looks). One definition, two consumers.

# Vehicle shape pack (packs/vehicles/*.json), compiled in like the built-in rig.
file(GLOB DD_VEHICLE_FILES CONFIGURE_DEPENDS ${PROJECT_SOURCE_DIR}/packs/vehicles/*.json)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${DD_VEHICLE_FILES})
set(DD_VEHICLE_TABLE "")
foreach(file ${DD_VEHICLE_FILES})
    get_filename_component(name ${file} NAME_WE)
    file(READ ${file} content)
    string(APPEND DD_VEHICLE_TABLE "        {\"${name}\", R\"DDVEH(${content})DDVEH\"},\n")
endforeach()
configure_file(src/builtin_vehicles.cpp.in ${CMAKE_CURRENT_BINARY_DIR}/generated/builtin_vehicles.cpp @ONLY)

add_library(dragdoll_scenarios STATIC
    include/dragdoll_scenarios/scenario.hpp
    include/dragdoll_scenarios/vehicles.hpp
    src/scenarios.cpp
    src/vehicles.cpp
    ${CMAKE_CURRENT_BINARY_DIR}/generated/builtin_vehicles.cpp
)
target_include_directories(dragdoll_scenarios PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(dragdoll_scenarios PUBLIC DragDoll::dragdoll PRIVATE nlohmann_json::nlohmann_json)
dragdoll_set_compile_options(dragdoll_scenarios)
