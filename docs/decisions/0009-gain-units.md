# ADR-0009: Stiffness as frequency and damping ratio

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Raw Kp/Kd depend on segment mass and inertia and don't transfer across limbs or characters.

## Decision
Express joint stiffness as **natural frequency (Hz)** and **damping ratio (zeta)**, matching Jolt `SpringSettings`. Tension profiles scale frequency and torque limits.

## Consequences
Mass-independent tuning; presets transfer between rigs.

## Sources
Jolt SpringSettings
