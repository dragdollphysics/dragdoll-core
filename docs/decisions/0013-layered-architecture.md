# ADR-0013: TeX/LaTeX-style layering

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Contributors need to know where their work belongs; modders need a stable core.

## Decision
**Layer 0 - `core/` + `capi/`**: physics, rig, motors, sensors, pose I/O, recording; strict review, semver. **Layer 1 - `behaviours/`**: standard behaviours, which may include **only** `behaviour_sdk.h` (dogfooding, like LaTeX written in TeX). **Layer 2 - `packs/`**: data (rigs, vehicles, presets). The behaviour SDK becomes a stable C ABI only once it stabilises (>= v0.2). No core freeze before 1.0.

## Consequences
Every built-in behaviour proves the extension API. Folder rules enforced in CMake (the behaviours target cannot see core internals).

## Sources
Knuth, TeX; Lamport, LaTeX
