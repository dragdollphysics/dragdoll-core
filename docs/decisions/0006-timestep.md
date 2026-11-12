# ADR-0006: Fixed 120 Hz step with interpolated output

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Stiff articulated rigs are unstable at 60 Hz; hosts render at variable rates (SA ~30 fps).

## Decision
Fixed simulation rate, default **120 Hz** (configurable 60-240). `dragdoll_world_step(dt)` accumulates host time and runs whole steps. Output poses are **interpolated** between the last two simulation states.

## Consequences
Output lags simulation by less than one step. Hosts never pass a fixed dt.

## Sources
G. Fiedler, 'Fix Your Timestep!'
