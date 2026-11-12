# ADR-0012: Single source of truth for Jolt build flags

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Mismatched Jolt preprocessor defines between the library and its consumers silently corrupt memory layouts.

## Decision
All Jolt defines live in one CMake interface target, `dragdoll_jolt_config`, linked by every target: `JPH_DEBUG_RENDERER` only in workbench builds, `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED` in Debug, no `JPH_DOUBLE_PRECISION`.

## Consequences
Never set Jolt defines anywhere else.

**Amended 2026-11-12 (Phase 1, task 1):**
* The single source of truth is `cmake/DragDollJolt.cmake` (Jolt pinned at `v5.6.0`); `dragdoll_jolt_config` links `Jolt::Jolt`, whose PUBLIC defines and `/arch` options propagate to every consumer.
* Debug renderer and profiler are controlled per **preset** (`DRAGDOLL_DEV_TOOLS`): ON in dev builds, OFF in the shipped SA build. A per-target switch isn't possible because Jolt is built once per configuration.
* CPU level: x64 dev builds use AVX2 + FMA; x86 builds use a portable SSE4.2 baseline (`DRAGDOLL_PORTABLE_CPU`).
* Jolt compiles itself with its own tuned flags (including MSVC `/fp:fast` unless cross-platform determinism is on). Jolt guarantees same-binary determinism with these flags; DragDoll's own code stays on `/fp:precise` (ADR-0020).
* With the Ninja generator Jolt doesn't see `CMAKE_VS_PLATFORM_NAME`; DragDollJolt.cmake sets it so Jolt emits its instruction-set flags.

## Sources
Jolt build documentation
