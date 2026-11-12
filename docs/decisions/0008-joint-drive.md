# ADR-0008: Jolt constraint motors for pose tracking

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
Hand-written explicit PD controllers are unstable at high stiffness and fixed timestep; Jolt motors are solved implicitly inside the constraint solver.

## Decision
Track target poses with Jolt **constraint motors** (`SwingTwistConstraint` / `HingeConstraint` in position mode with torque limits), via `JPH::Ragdoll::DriveToPoseUsingMotors`. Explicit torques are used only for balance and virtual forces (ADR-0003).

## Consequences
Stable stiff tracking; our work is gain scheduling and behaviour control, not motor math.

## Sources
Jolt Ragdoll sample; Tan, Liu, Turk 2011 (Stable PD) as fallback reference
