# ADR-0004: Units, handedness and up axis

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Hosts differ: SA and Unreal are Z-up, Godot and three.js are Y-up.

## Decision
SI units (m, kg, s, rad); right-handed; quaternions stored `(x, y, z, w)`. **Up axis is a per-world setting** (default +Y). Core code always uses the world up vector and gravity direction; no hardcoded axis anywhere.

## Consequences
Every balance/perception computation takes `up` from the world. Unit tests run with both +Y and +Z up.

## Sources
workplan D4
