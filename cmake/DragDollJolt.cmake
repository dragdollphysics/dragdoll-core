# Jolt Physics: fetched at a pinned release and configured in ONE place (ADR-0012).
# Never set Jolt options or JPH_* defines anywhere else. Consumers link `dragdoll_jolt_config`,
# which carries Jolt's PUBLIC defines so every target sees the same configuration.

include(FetchContent)

set(DRAGDOLL_JOLT_TAG "v5.6.0" CACHE STRING "Pinned Jolt Physics release tag")

# --- Jolt build options (Jolt uses CMake 3.20, so normal variables override its option() defaults) ---
set(DOUBLE_PRECISION OFF)                     # ADR-0012
set(CROSS_PLATFORM_DETERMINISTIC OFF)         # ADR-0007: opt-in only
set(JPH_BUILD_SHARED_LIBS OFF)                # Jolt is linked statically into dragdoll.dll
set(USE_STATIC_MSVC_RUNTIME_LIBRARY ON)       # ADR-0015
set(OVERRIDE_CXX_FLAGS ON)                    # Jolt's own tuned flags, scoped to Jolt's directory
set(INTERPROCEDURAL_OPTIMIZATION OFF)         # Keep link times short; revisit for release builds
set(FLOATING_POINT_EXCEPTIONS_ENABLED ON)     # Jolt enables these only in Debug/Release configs
set(CPP_EXCEPTIONS_ENABLED OFF)
set(CPP_RTTI_ENABLED OFF)
set(ENABLE_ALL_WARNINGS OFF)                  # Jolt's warnings are Jolt's business
set(ENABLE_OBJECT_STREAM OFF)                 # Not needed; smaller binary
set(ENABLE_INSTALL OFF)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")       # Single-config (Ninja) builds: asserts in Debug only
    set(USE_ASSERTS ON)
else()
    set(USE_ASSERTS OFF)
endif()

# No GPU compute backends (DX12 / Vulkan / Metal / CPU compute): not needed, avoids SDK dependencies.
set(JPH_USE_DX12 OFF)
set(JPH_USE_VK OFF)
set(JPH_USE_MTL OFF)
set(JPH_USE_CPU_COMPUTE OFF)

# Dev tools: debug renderer + profiler in dev builds, off for shipped binaries.
set(DEBUG_RENDERER_IN_DEBUG_AND_RELEASE ${DRAGDOLL_DEV_TOOLS})
set(PROFILER_IN_DEBUG_AND_RELEASE ${DRAGDOLL_DEV_TOOLS})
set(DEBUG_RENDERER_IN_DISTRIBUTION OFF)
set(PROFILER_IN_DISTRIBUTION OFF)

# CPU feature level. Portable = SSE4.2 baseline for players' older machines (32-bit SA build).
if(DRAGDOLL_PORTABLE_CPU)
    set(USE_SSE4_1 ON)
    set(USE_SSE4_2 ON)
    set(USE_AVX OFF)
    set(USE_AVX2 OFF)
    set(USE_AVX512 OFF)
    set(USE_LZCNT OFF)
    set(USE_TZCNT OFF)
    set(USE_F16C OFF)
    set(USE_FMADD OFF)
else()
    set(USE_AVX512 OFF)                       # Reference machine (Ryzen 9 3900X) has no AVX-512
endif()

# Jolt only emits its /arch flags and JPH_USE_* defines when CMAKE_VS_PLATFORM_NAME is x86/x64,
# which only the Visual Studio generator sets. With Ninja it stays empty and Jolt silently builds
# for plain SSE2, so provide it here for MSVC builds.
if(MSVC AND NOT CMAKE_VS_PLATFORM_NAME)
    if(CMAKE_SIZEOF_VOID_P EQUAL 4)
        set(CMAKE_VS_PLATFORM_NAME "x86")
    else()
        set(CMAKE_VS_PLATFORM_NAME "x64")
    endif()
endif()

FetchContent_Declare(
    JoltPhysics
    GIT_REPOSITORY https://github.com/jrouwe/JoltPhysics.git
    GIT_TAG        ${DRAGDOLL_JOLT_TAG}
    GIT_SHALLOW    TRUE
    SOURCE_SUBDIR  Build
)
FetchContent_MakeAvailable(JoltPhysics)

# The single configuration target every DragDoll target links against.
add_library(dragdoll_jolt_config INTERFACE)
target_link_libraries(dragdoll_jolt_config INTERFACE Jolt::Jolt)
