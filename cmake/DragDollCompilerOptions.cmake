# Compiler options for DragDoll's own targets (ADR-0020). Jolt keeps its own flags.

function(dragdoll_set_compile_options target)
    if(MSVC)
        target_compile_options(${target} PRIVATE
            /W4
            /permissive-
            /utf-8
            /Zc:__cplusplus
            /fp:precise   # Never fast-math in DragDoll code: determinism (ADR-0007)
            /EHsc         # Std library stays well-defined; nothing crosses the C ABI (ADR-0020)
        )
        if(DRAGDOLL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
        if(CMAKE_SIZEOF_VOID_P EQUAL 4)
            target_compile_options(${target} PRIVATE /arch:SSE2)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
        if(DRAGDOLL_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()
