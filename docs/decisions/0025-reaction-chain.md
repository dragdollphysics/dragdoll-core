# ADR-0025: The reaction chain — stages, reflex layers, vitality, behaviour SDK

- **Status:** Accepted
- **Date:** 2026-11-16

## Context
ADR-0024 gives a character senses and leaves the trajectory to physics. This ADR decides what the character *does*
with its muscles from the moment a vehicle becomes a threat until the host takes it back, and how behaviours are
written (ADR-0013: Layer 1 sees only `behaviour_sdk.h`).

Facts from the literature that shape the design:
* **People react slowly compared with an impact.** Volunteers facing a simulated accident mostly ran, stepped back or
  froze; mean reaction times were about 0.5 s (younger) to 0.7 s (older); posture at impact varied widely whatever the
  reaction (Soni et al.). A startle is faster (tens of milliseconds to the neck, spreading down the body) but is a
  stereotyped tensing, not a chosen movement (Brown et al.).
* **Falls are not random.** Unexpected falls from standing height show a repeatable protective pattern: hands arrive
  with or just before the pelvis, the head is kept off the ground (Hsiao & Robinovitch). How the arms meet the ground
  matters: a slightly flexed, yielding elbow cut peak hand force by about 27-40 % against natural and stiff-arm
  landings (DeGoede & Ashton-Miller).
* **An airborne body can only redistribute what it has.** With no external torque, changing the moment of inertia
  (tuck / extend) changes the spin rate and so which part lands first; landing then spreads the impact over several
  body parts by rolling (Ha, Ye & Liu).
* Simulated characters that react to hits by changing joint stiffness over a tracked pose, then recovering, read as
  alive (Zordan & Hodgins); controllers compose when each declares when it can take over (Faloutsos et al.).

## Decision

### 1. Behaviours command muscles, nothing else
A behaviour reads a **percept** (ADR-0024) and writes a **command**: tension per body region, a target pose (joint
offsets in rig axes), balance on/off. No forces, no velocities, no teleports (ADR-0003). Whatever the body cannot do
with its own muscles, it does not do.

Core additions this needs (Layer 0): **tension per region** (head-neck, trunk, arm L/R, leg L/R; today it is per
character) and an **aim** helper that turns "point this limb at that world position, this much extended" into
shoulder/elbow or hip/knee targets (analytic two-bone solve; it only sets a target, the muscles still have to get
there).

### 2. Human limits are parameters, not style
* **Reaction clock.** Every new stimulus (first sight of the threat, first contact, leaving the ground) starts a
  timer. *Reflex* layers may start after the reflex latency (default 0.1 s); *voluntary* actions after the voluntary
  latency (default 0.5 s). Consequence, wanted: a victim who never saw the car goes through the impact almost
  passively and only starts protecting itself in the air; a victim with a second of warning arrives braced.
* **Vitality** `v` in [0, 1] (alertness and strength). It scales tension and gates stages. It drops with the head's
  velocity change and with large whole-body velocity changes (ADR-0024 severity); the host may set it (the Phase 6
  outcome tiers: *killed* -> 0 after the impact). At `v = 0` the chain stops and the body is the limp tier.
* **Awareness** in [0, 1]: by default from geometry (threat inside the head's forward field of view, long enough
  ago); the host may override it (a game knows whether its pedestrian was looking, fleeing or distracted).

### 3. Stages (exclusive) — the vocabulary
Plain words from biomechanics and martial arts; none taken from any proprietary product (ADR-0014).

| Stage | Entered when | Muscles |
|---|---|---|
| **Watch** | host-animated, a threat is tracked | none (the host's animation) |
| **Anticipate** | threat, aware, time to collision < ~1.2 s | reflex: *Startle* (tense, shoulders up, slight crouch, head to the threat); voluntary: *Shield*, in one of the two shapes volunteers showed — **cover** (arms in a triangle over face and head) or **fend** (hands toward the vehicle, body turned to face it) |
| **Impact** | first vehicle contact (the handover: kinematic -> dynamic with the animated velocities) | struck leg yields, trunk and neck co-contract; if Shield was up, hands go to the bonnet before the trunk arrives |
| **Carried** | supported by the vehicle, not by the ground | *Guard*, mild *Curl*; tension eases from tense to relaxed |
| **Airborne** | no contact for 25 ms | if time to ground and vitality allow: *Reach* toward the ground on the leading side, chin tucked, tuck or extend so the limbs — not the head — arrive first; otherwise *Guard* only |
| **Landing** | ground contact, or predicted within ~120 ms | break-fall: arms meet the ground slightly flexed and yielding, then the trunk goes relaxed so momentum rolls on instead of stopping |
| **Tumble** | on the ground and still moving (> ~1 m/s) | relaxed + *Guard*; never fights the slide |
| **Settle** | nearly still for ~0.3 s | eases into the **host's rest pose** that matches how the body lies (see §3a); low vitality: slower and weaker, down to limp |
| **Handback** | settled (existing state + facing hints, ADR-0017) | none: the host blends the small remainder, continues its own animation, and the ragdoll is destroyed |

Stages may be skipped (a 10 km/h knock-down goes Impact -> Tumble; a forward projection has no Carried). Wrap, vault
and projection are **not** stages: they are what happens between Impact and Landing, and they are measured.

### 3a. Rest poses: one animation slot replaced, nothing left behind (amended 2026-11-16, founder's design)
*Amended 2026-11-17 by ADR-0027 for game hosts:* the game decides and DragDoll only draws - the start is the game's own hit, the release is the game's own next step (its get-up; a corpse stays a sleeping ragdoll until the game moves on). No rest pose is forced on the game.
The chain replaces exactly one thing in the host: the animation of being hit. It starts from the host's pose and
velocities at the hit (activation from motion) and ends in the pose the host's own animation would have ended in.
* The host gives the character its **rest poses** (for San Andreas: lying face up and lying face down, read from the
  game's own animations at runtime — nothing bundled).
* In Settle, DragDoll picks the rest pose matching the body's facing and drives the muscles toward it (target pose +
  low tension). Muscles only: a body does not flip itself over; one lying on its side rolls to the nearer pose; on a
  slope, a kerb or a bonnet the match is approximate.
* At Handback the host moves its character to the ragdoll's root (position and heading), blends the remaining pose
  difference over a fraction of a second, and carries on as it always did (stay dead or get up). **The ragdoll is
  destroyed for every outcome**: a crowded scene leaves ordinary host corpses, not sleeping ragdolls. A killed victim
  (vitality 0) cannot ease into the pose itself, so the host's blend covers all of it.
* Other animations (lying dead, getting up, walking, ...) are later slots replaced one at a time through the same
  workflow (ADR-0018); none is needed for v1.

### 3b. Checked against the literature (2026-11-16, `docs/reference/pedestrian-impact.md`)
* The head strikes the vehicle 50-140 ms after first contact in most real cases; a voluntary reaction takes about
  500 ms (700 ms for older people). So nothing decided after first contact can act before the head strike: protection
  on the bonnet comes from Anticipate or from reflexes only. This is the reaction clock doing its job, not a tuning
  choice.
* People who got clear needed at least 1.6-1.8 s to collision when they noticed the vehicle. Below that, reactions
  change the posture at impact, not whether it happens — which is exactly the window Anticipate covers.

### 4. Reflex layers (additive, may run with any stage that allows them)
* **Guard** — protect the head: chin tucked, forearms between the head and the nearest surface it is approaching.
* **Reach** — hands (or feet) toward the surface about to be hit, elbows / knees slightly flexed.
* **Curl** — flex trunk, hips and knees: less exposed, smaller moment of inertia.
* **Startle** — brief whole-body tensing with raised shoulders.
Layers write into the same command; a stage lists which layers it allows and their order of priority per region.

### 5. Behaviour SDK (`behaviours/include/behaviour_sdk.h`)
* C++ interface, header only: `Percept` (read-only snapshot), `Command`, `Behaviour::update(percept, command, dt)`,
  the pose library and the aim helper. The car-hit chain is one behaviour made of stage and layer objects; nothing in
  it may include core headers (enforced in CMake).
* All numbers (latencies, thresholds, poses, tension choices) live in `packs/behaviours/car_hit.json`: tunable live in
  the workbench and, in Phase 5, by the lab.
* Host API: `dragdoll_character_set_behaviour(character, "car_hit")`, `set_awareness`, `set_vitality`. They are
  recorded; behaviour state itself is a deterministic function of the simulation, so replays stay bit-identical
  without recording it. The current stage, layers and vitality are reported in `DdCharacterInfo`, the live tap and the
  recording's event stream.
* A stable C ABI for third-party behaviours waits until the SDK settles (ADR-0013).

### 6. Out of scope for v1
Stepping and diving out of the way (the host's own locomotion covers running and stepping back: the puppet follows
the animation, and the handover inherits its velocity); getting up (the host's animation); grabbing the vehicle;
reactions to anything but vehicles.

## Consequences
* Every stage and layer is small, has one trigger and one job, and gets its own scenario and metric; "looks odd" from
  the founder maps to one of them.
* The unaware / aware / killed variety comes from three numbers (awareness, vitality, the two latencies), not from
  separate animations — and it is the variety the literature reports.
* Honest limits will show: a braced pedestrian is still thrown. That is the point (ADR-0003), and the comparison with
  existing mods is on believability, not on survivability.
* Risks: per-region tension changes the muscle code (ADR-0008/0009 stay valid: still frequency + damping, per
  region); the aim helper must respect joint limits (shoulder cones); stage chatter at thresholds needs hysteresis and
  minimum stage times; reflex latency and vitality constants need sourcing or tuning in task 4.2 / Phase 5.

## Sources
* A. Soni, T. Robert, F. Rongiéras, P. Beillas, "Observations on Pedestrian Pre-Crash Reactions during Simulated
  Accidents", Stapp Car Crash Journal 57, 2013 (SAE 2013-22-0006); EU project PEDPCREACT final report summary (CORDIS)
  — reaction types and times.
* P. Brown, J. C. Rothwell, P. D. Thompson, T. C. Britton, B. L. Day, C. D. Marsden, "New observations on the normal
  auditory startle reflex in man", Brain 114(4), 1991, pp. 1891-1902 — startle pattern and recruitment order.
* E. T. Hsiao, S. N. Robinovitch, "Common protective movements govern unexpected falls from standing height", Journal
  of Biomechanics 31(1), 1998, pp. 1-9.
* K. M. DeGoede, J. A. Ashton-Miller, "Fall arrest strategy affects peak hand impact force in a forward fall", Journal
  of Biomechanics 35(6), 2002, pp. 843-848.
* S. Ha, Y. Ye, C. K. Liu, "Falling and Landing Motion Control for Character Animation", ACM Transactions on Graphics
  31(6), 2012 (SIGGRAPH Asia).
* V. B. Zordan, J. K. Hodgins, "Motion capture-driven simulations that hit and react", ACM SIGGRAPH / Eurographics
  Symposium on Computer Animation, 2002.
* P. Faloutsos, M. van de Panne, D. Terzopoulos, "Composable controllers for physics-based character animation",
  SIGGRAPH 2001.
* ADR-0003, ADR-0013, ADR-0014, ADR-0017, ADR-0023, ADR-0024.
