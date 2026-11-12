# ADR-0016: Continuous collision for fast impacts

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
At 120 km/h a vehicle moves ~28 cm per 120 Hz step, more than a shin's thickness, so discrete collision tunnels.

## Decision
Limbs and vehicle proxies use Jolt continuous collision (`EMotionQuality::LinearCast`) from day one.

## Consequences
Slight extra cost per body; correct car-hit contacts at all speeds.

## Sources
Jolt documentation: motion quality
