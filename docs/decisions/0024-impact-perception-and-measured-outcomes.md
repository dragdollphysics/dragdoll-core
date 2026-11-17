# ADR-0024: Impact perception, and trajectories as measured outcomes

- **Status:** Accepted
- **Date:** 2026-11-16

## Context
Phase 4 is the car-hit vertical slice: a pedestrian struck by a vehicle should wrap onto the bonnet, be projected
forward, or vault, depending on the vehicle's front shape and speed, then fly, land and settle. Two questions come
before any behaviour: **what can a character know** about the vehicle and about itself, and **who decides the
trajectory**.

The pedestrian-impact literature classifies post-impact trajectories (wrap, forward projection, fender vault, roof
vault, somersault) and explains them mechanically: the class follows from the vehicle's front geometry relative to the
pedestrian's centre of gravity, the impact speed and braking. Forward projection happens when the centre of gravity
lies within the primary impact zone (high, flat fronts; children); wrap when the body is struck below it.

Hosts differ in what they can tell us. The workbench has vehicle profiles (`packs/vehicles`); San Andreas gives only
collision geometry (13-25 spheres and a few triangles per car, streamed live since bridge task 3.7). A design that
needs a vehicle database per host fails ADR-0018 (any game).

## Decision

### 1. Physics decides the trajectory; DragDoll measures it
The trajectory class is **never selected, scripted or assisted**. It emerges from the rig, the vehicle geometry,
friction and speed. DragDoll *predicts* it (to prepare a reaction) and *measures* it (to test itself against the
literature). If the passive (limp) body does not produce the literature's classes for the standard vehicle classes,
the fix belongs in the rig, the vehicle shape or the contact parameters — not in a behaviour. This extends ADR-0003
(internal torques only) from balance to impacts.

### 2. Collider groups
`DdColliderDesc.group` (uint32, 0 = none): the parts of one object are perceived as one thing (a car is one threat,
not 25). A group's velocity comes from its kinematic targets. Recorded like any collider field.

### 3. Senses (Layer 0, every fixed step, deterministic, read-only for behaviours)
* **Contacts:** per character body: the other collider and its group, contact point and normal, and whether the other
  side is static (ground / level) or moving (vehicle). Collected by a contact listener; no solver internals.
* **Severity:** each body's velocity change across the step (Δv). Head Δv and whole-body Δv are the inputs to
  vitality (ADR-0025). Δv needs no impulse reconstruction and is what a recording can replay exactly.
* **Self state:** centre of mass, its velocity, angular momentum about it; trunk uprightness; which bodies are
  supported by the ground or by a vehicle; **airborne** (no contact at all for >= 3 steps, 25 ms); **ground probe**
  (ray down from the centre of mass: height and ground normal); **landing prediction** (ballistic time to ground and
  the body predicted to arrive first); stillness (existing `still_seconds`).
* **Threat:** for every group moving faster than 1 m/s within 30 m: closing motion in the horizontal plane relative to
  the character's centre of mass -> **time to collision** and **miss distance** against the group's swept, oriented
  bounding box. A threat exists when the miss distance is below the group's half width + 0.3 m and time to collision
  is below 2 s; only the most imminent one is kept. Cost: a few vector operations per group.

### 4. Front profile probe (the vehicle database every host already has)
When a threat first appears, the front of that group is measured once from its own collision geometry: horizontal
rays against that group only, along its direction of travel, at the predicted impact line, every 0.1 m from 0.1 to
2.0 m above the ground. The result is the front silhouette as a polyline (setback vs height), split by slope into
**front face** (near vertical), **bonnet** (< ~20 deg), **windscreen** (~20-70 deg) and **roof**. From it:
bumper height, leading-edge height, bonnet length and slope, roof height.
* **Predicted class:** leading edge at or above the character's centre-of-mass height -> forward projection expected;
  below it -> wrap expected (Wood, Simms & Walsh). Used by behaviours to prepare, never to force.
* **Contact zones:** a vehicle contact is labelled bumper / leading edge / bonnet / windscreen / roof by the nearest
  polyline segment in the vehicle's frame.
* Cached per group (about 20 ray casts, once per vehicle per approach). It works unchanged for a workbench profile
  box and for San Andreas spheres and triangles.

### 5. Impact report (per character, per impact; in recordings, the live tap and `dragdoll-inspect`)
Events with times: first contact (body, zone, vehicle speed, lateral offset), contact order, head strike (zone and
**wrap-around distance**: length along the silhouette from the ground to the head contact), separation and the
**projection speed** of the centre of mass at separation, apex height, first ground contact (body), rest position,
**throw distance** (horizontal, impact point to rest), time to settle, peak head Δv.
* **Measured class** (rules ours, after the Ravani et al. categories; thresholds reviewed against the full texts in
  task 4.2):
  * *forward projection* — no trunk or head contact above the leading edge; body ahead of the vehicle at separation;
  * *wrap* — trunk or head contact on bonnet or windscreen **and** the centre of mass carried past the leading edge
    while in contact; body leaves forward of the vehicle (amended 2026-11-17: on high fronts the head and chest fold
    onto the bonnet while the body is pushed ahead - forward projection with a head strike, Simms 2013);
  * *roof vault* — centre of mass passes over the roof line; lands behind the vehicle's front;
  * *fender vault* — first contact within about 0.15 m of a front corner; body leaves sideways;
  * *somersault* — wrap with more than 270 deg of pitch rotation before ground contact.
* **Throw-distance corridor** (amended 2026-11-16 after the reading in `docs/reference/pedestrian-impact.md`): the
  throw distance from the impact point is carry + flight + slide. The corridor applies to flight + slide from the
  **separation point**, for the measured launch speed `u` and launch height `h` of the centre of mass:
  `mu h + u^2 / (2 mu g) <= s <= mu h + u^2 (1 + mu^2) / (2 mu g)` (the general throw equation at its extreme launch
  angles), with body-on-road friction `mu` 0.66-0.84. The ratio `u / vehicle speed` is reported per class; its
  literature ranges are still to be read (reference file §7).
  *Amended 2026-11-17 (task 4.3):* the corridor is the general equation at the **measured** launch angle
  `mu h + u^2 (cos theta + mu sin theta)^2 / (2 mu g)` over `mu` 0.66-0.84 (bodies leaving a car often head slightly
  downward, which the 0-45 deg form wrongly called short); it applies only to a **real flight** (the feet off the road
  at separation; otherwise `in_corridor = -1`); and the separation is the **last** time the body leaves the object
  before its first ground contact (a bounce on the bonnet does not count).
  *Amended 2026-11-17 (literature read):* the **total throw distance** (impact to rest) is also checked against the
  real-world speed-distance bands of Wood, Simms & Walsh 2005 (185 wrap / 21 forward-projection accidents, 0.1-99.9
  percentile; reference file §3); this is the primary throw check, the corridor the per-launch one.
* **Further checks from the literature** (reference file §6): head strike 50-140 ms after first contact, wrap-around
  distance 1.5-2.3 m for a 1.6-1.75 m adult, head speed at the strike 0.6-1.06 of the vehicle's speed, flight
  0.45-0.7 s at 33-49 km/h, slide deceleration 6.5-8.3 m/s².

### 6. Order of work: the passive baseline comes first
Before any reaction exists: limp body x standard vehicle classes x 10-80 km/h x seeds, classified by the impact report.
Phase 4 behaviours start only once that table matches the literature's pattern (sedan -> wrap; SUV / van -> forward
projection; bus -> never a roof vault).

## Consequences
* One perception layer serves every host: no per-game vehicle tables; a new car model in a mod just works.
* Outcomes are honest and testable: the acceptance numbers in the workplan (wrap rate for a sedan at 30 km/h, etc.)
  are measurements of the physics, and the lab (Phase 5) can optimise against them without a way to cheat.
* The limp tier (killed outcome, Phase 6) is simply this baseline: already validated, nearly free.
* Costs to watch: the contact listener on every character body (budget <= 0.1 ms per character per step, ADR-0015);
  sphere-built San Andreas cars are bumpy for a sliding body — a smooth hull per car (convex hulls from its own parts)
  is a dragdoll-sa task before in-game tuning.
* Sourced since (2026-11-16, `docs/reference/pedestrian-impact.md`): head-strike times, wrap-around distance, flight
  and slide values, body-on-road friction. Still open: body-on-bonnet friction, launch-speed ratios per class,
  conditions for the rarer trajectory types.

## Sources
* B. Ravani, D. Brougham, R. T. Mason, "Pedestrian Post-Impact Kinematics and Injury Patterns", SAE Technical Paper
  811024, 25th Stapp Car Crash Conference, 1981 — trajectory categories.
* D. P. Wood, C. K. Simms, D. G. Walsh, "Vehicle-pedestrian collisions: validated models for pedestrian impact and
  projection", Proc. IMechE Part D 219(2), 2005, pp. 183-195 — wrap vs forward projection, centre of gravity vs impact
  zone, projection phases (open access, TCD repository).
* J. A. Searle, A. Searle, "The Trajectories of Pedestrians, Motorcycles, Motorcyclists, etc., Following a Road
  Accident", SAE Technical Paper 831622, 1983; J. A. Searle, SAE 930659, 1993 — projection speed vs distance, friction.
* C. K. Simms, D. P. Wood, "Pedestrian and Cyclist Impact: A Biomechanical Perspective", Springer, 2009.
* ADR-0003 (physical honesty), ADR-0014 (clean room), ADR-0016 (fast impacts), ADR-0018 (any game).
