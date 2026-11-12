# ADR-0012: Single source of truth for Jolt build flags

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Mismatched Jolt preprocessor defines between the library and its consumers silently corrupt memory layouts.

## Decision
All Jolt defines live in one CMake interface target, `dragdoll_jolt_config`, linked by every target: `JPH_DEBUG_RENDERER` only in workbench builds, `JPH_FLOATING_POINT_EXCEPTIONS_ENABLED` in Debug, no `JPH_DOUBLE_PRECISION`.

## Consequences
Never set Jolt defines anywhere else.

## Sources
Jolt build documentation
