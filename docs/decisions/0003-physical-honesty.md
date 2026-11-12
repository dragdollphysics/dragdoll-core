# ADR-0003: Internal torques only; explicit assist budget

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
The root (pelvis) is unactuated. Applying torque to it 'from nowhere' looks robotic and hides controller weakness.

## Decision
Balance and behaviours apply **internal torques only** (equal-and-opposite pairs between connected bodies, e.g. via the stance hip). An optional **assist** force/torque exists per character: explicit, capped (N, N·m), logged in recordings, **off by default**, and must be **0 in all acceptance and lab runs**.

## Consequences
Harder controllers, honest results. Assist remains available to hosts who prefer robustness over purity.

## Sources
Yin, Loken, van de Panne 2007 (SIMBICON stance-hip torque)
