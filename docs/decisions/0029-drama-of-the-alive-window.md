# ADR-0029: The drama of the alive window (falls, startle, pain behaviour)

- **Status:** Accepted (founder, 2026-11-19; built and reviewed step by step, task 4.11)
- **Date:** 2026-11-19

## Context
A pedestrian the game lets live gets about 2 s from the hit to the game's own get-up (measured, devlog 2026-11-19).
The founder asked for more drama in those 2 s. Every technique must cite a published source (ADR-0014); the founder's
reviews in the live viewer decide what looks right. The before-the-hit stage stays disabled in the game (the game
animates that part, ADR-0027).

## Decision
`car_hit` (ADR-0025) gains the following; every number is a parameter in `packs/behaviours/car_hit.json`.

1. **Startle at the hit.** At the reflex latency after the first contact the arms jerk tense into pose `startle_hit`
   (head and upper back forward, upper arms forward and in, elbows bent, shoulders shrugged), held 0.25 s before any
   guard or reach, then the arms ease to braced. Sources: the startle pattern (Brown et al. 1991, *Brain* 114:1891;
   Landis & Hunt 1939); the wrist's first upward beat in unexpected falls (Hsiao & Robinovitch 1998, *J Biomech* 31:1,
   Fig. 6). The shrug needs a shoulder girdle: the rig becomes `humanoid18` with two clavicle bodies (2.5 % mass each).
2. **Catching the fall.**
   * The reach turns the palms to the ground (wrist target in `aimLimb`; wrist twist range 85 deg).
   * The hands do not race the body to the ground: they aim at the height of the lowest trunk / head body
     (`reach.hover`) and come down with it, and once within 0.4 m of the ground the elbow bends (`reach.brake_*`) - the
     wrist brakes and lands with the pelvis. Source: Hsiao & Robinovitch 1998 (wrist and pelvis within 50 ms in 72 % of
     falls; the wrist at about 2/3 of its peak speed).
   * Backward falls put the hands beside the hips (the upper hand of a back-and-sideways fall across to the lower hip);
     bodies that slide or tip off the vehicle reach too; the reach starts once the head is below 1.2 m and dropping.
   * On landing the arms give (DeGoede & Ashton-Miller 2002, *J Biomech* 35:843).
   * In a sideways fall, not already rolling, the spine and neck turn the chest to the ground late in the descent
     (Hsiao & Robinovitch 1998, Fig. 7). Rare in car hits (fast, long flights only).
3. **Pain behaviour while down** (Keefe & Block 1982, *Behav Ther* 13:363: guarded movement, bracing, rubbing,
   grimacing, sighing; Williams 2002, *Behav Brain Sci* 25:439: pain shows in the resting phase, and held-back pain
   reads as more real than shown pain).
   * Hurt starts only when something above the legs lies on the ground. On the back the knees bend with the feet on the
     ground; on a side or the front the curl.
   * Guarding: the hurt part's region tense (held still), a leg that does not hurt relaxed; the holding arms braced.
   * Rubbing: each hand circles slowly over the spot (3.5 cm, 0.9 Hz, half a turn apart).
   * Breathing: quick, shallow breaths (0.45 Hz) - the chest opens and the shoulders lift on each inhale - and a sigh
     every fifth breath.
   * Holding back: every 3-6 s a small 0.25 s wince (curl tightens, shoulders hunch, breath and rubbing pause).
4. **Measured against people.** Analyzer rule kind `population`: a published share of people, compared over a sweep
   (a single run outside it is no defect). Four rules from Hsiao & Robinovitch 1998, counted as the study did (a part
   within 100 mm of the surface has landed), applied to knock-downs only (up to 20 km/h): Campolettano et al. 2023 (ESV
   23-0075, 42 dash-camera collisions) found every pedestrian knocked over above 10 mph, injured by the fall from
   standing height. Faster hits land differently (video studies: legs first in about half, the head in about 30 %;
   Han et al. 2018 / 2019, not yet read) and need their own rules.

## Consequences
* Knock-downs (1 500-run sample, people in brackets): both hands land 52 % (93), hand and pelvis within 50 ms 68 % (72),
  the hand braked 66 % (84), the head not first 99 % (82). The remaining both-hands gap is mostly backward falls and
  bodies landing on a side, where the upper hand cannot reach the ground without the body rolling.
* The viewer shows what the doll does: black palms, dots where the vehicle hit, the vehicle half see-through, and an
  optional stand-up after about 2 s.
* Open: palms facing the hurt spot; hands reaching a hurt leg; references for fast-hit landings (Han et al.); whether
  to lengthen the game's 2 s down time.
