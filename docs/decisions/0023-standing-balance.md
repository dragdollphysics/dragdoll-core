# ADR-0023: Standing balance via capture point, ankle and hip strategies

- **Status:** Accepted
- **Date:** 2026-11-13

## Context
Phase 3 needs a character that stays upright under small disturbances (acceptance: `hold_stance` upright 60 s under
5 N·s pokes; `bump_5kmh` absorbed) without stepping. ADR-0003 allows internal torques only; ADR-0009 gives joint drive
as frequency + damping. Muscles already track a target pose (task 3.1/3.2) with stiffness from each joint's effective
inertia.

## Decision
A per-character balance controller, run every fixed step before the muscles:
* **Measure** the centre of mass (mass-weighted body positions) and its velocity (from the last fixed step), the support
  centre (midpoint of the feet) and the height of the centre of mass above the feet.
* **Capture point** (extrapolated centre of mass) `xCP = x + v / ω`, `ω = sqrt(g / h)`; its horizontal offset from the
  support centre, split into the character's forward and lateral axes, is the balance error.
* **Ankle strategy:** both feet get a target offset proportional to the error (forward error → plantar flexion, lateral
  error → inversion/eversion), clamped to the joint ranges. With the feet on the ground, the ankle motors push the body
  back over its feet through ground contact — the inverted-pendulum response of quiet standing.
* **Hip strategy:** when the forward error exceeds ~5 cm (beyond what the ankles can correct), the hips flex/extend in
  proportion to the excess, moving the trunk to bring the centre of mass back.
* Offsets are applied on top of the host's target pose (they never overwrite it). No external or "assist" forces.
* No stepping (later phases); balance requires a tension other than LIMP.

## Consequences
* Balance is a thin layer over the existing muscles: deterministic, recorded (on/off is a host call), cheap (a few
  vector operations per step).
* Gains are tuned against `hold_stance` / `bump_5kmh`; disturbances that push the capture point outside the feet still
  topple the character — the correct physical outcome until stepping exists.

## Sources
* J. Pratt, J. Carff, S. Drakunov, A. Goswami, "Capture Point: A Step toward Humanoid Push Recovery", IEEE-RAS
  Humanoids 2006.
* A. L. Hof, M. G. J. Gazendam, W. E. Sinke, "The condition for dynamic stability", Journal of Biomechanics 38 (2005).
* F. B. Horak, L. M. Nashner, "Central programming of postural movements: adaptation to altered support-surface
  configurations", Journal of Neurophysiology 55 (1986) — ankle and hip strategies.
* D. A. Winter, "Human balance and posture control during standing and walking", Gait & Posture 3 (1995).
