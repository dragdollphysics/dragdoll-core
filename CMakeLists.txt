cmake_minimum_required(VERSION 3.24)

project(DragDoll
    VERSION 0.0.1
    DESCRIPTION "Open-source active ragdoll library on Jolt Physics"
    HOMEPAGE_URL "https://github.com/dragdollphysics/dragdoll-core"
    LANGUAGES C CXX)

# ---------------------------------------------------------------------------
# Options
# ---------------------------------------------------------------------------
option(DRAGDOLL_PORTABLE_CPU      "Target older CPUs: SSE4.2 baseline, no AVX/AVX2/FMA (used for the 32-bit SA build)" OFF)
option(DRAGDOLL_DEV_TOOLS         "Enable Jolt debug renderer and profiler (dev builds; OFF for shipped binaries)" ON)
option(DRAGDOLL_BUILD_TESTS       "Build DragDoll tests" ON)
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND DRAGDOLL_DEV_TOOLS)
    set(_dd_viewer_default ON)
else()
    set(_dd_viewer_default OFF)
endif()
option(DRAGDOLL_BUILD_VIEWER      "Build the live scenario viewer (raylib; 64-bit dev builds only)" ${_dd_viewer_default})
option(DRAGDOLL_WARNINGS_AS_ERRORS "Treat DragDoll warnings as errors" ON)

# ---------------------------------------------------------------------------
# Global settings (ADR-0011, ADR-0015, ADR-0020)
# ---------------------------------------------------------------------------
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_C_STANDARD 11)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Static CRT everywhere: the SA plugin must not depend on a runtime DLL (ADR-0015).
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

# Put every DLL/EXE in one folder so tests find dragdoll.dll without PATH tricks.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

# Optional compiler cache.
find_program(DRAGDOLL_CCACHE ccache)
if(DRAGDOLL_CCACHE)
    message(STATUS "DragDoll: using ccache (${DRAGDOLL_CCACHE})")
    set(CMAKE_C_COMPILER_LAUNCHER   "${DRAGDOLL_CCACHE}")
    set(CMAKE_CXX_COMPILER_LAUNCHER "${DRAGDOLL_CCACHE}")
endif()

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(DragDollCompilerOptions)
include(DragDollJolt)

# ---------------------------------------------------------------------------
# Targets
# ---------------------------------------------------------------------------
add_subdirectory(core)
add_subdirectory(scenarios)

if(DRAGDOLL_BUILD_VIEWER)
    add_subdirectory(workbench)
endif()

if(DRAGDOLL_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
    add_subdirectory(tools/bench)
endif()

if(CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(_dd_arch "x86")
else()
    set(_dd_arch "x64")
endif()
message(STATUS "DragDoll ${PROJECT_VERSION}: arch=${_dd_arch} portable_cpu=${DRAGDOLL_PORTABLE_CPU} dev_tools=${DRAGDOLL_DEV_TOOLS}")
