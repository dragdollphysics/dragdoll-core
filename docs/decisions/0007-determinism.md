# ADR-0007: Determinism and golden hashes

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Exact replay powers hot reload, recordings, regression tests and the lab.

## Decision
Same binary + same inputs => bit-identical results regardless of thread count. Golden state hashes are stored **per platform** (win-x64, win-x86, later wasm). `JPH_CROSS_PLATFORM_DETERMINISTIC` is opt-in only.

## Consequences
No uncontrolled randomness (seeded RNG only), no wall-clock time in simulation, stable iteration orders. Golden hash updates must be explicit in a commit.

## Sources
Jolt documentation: deterministic simulation
