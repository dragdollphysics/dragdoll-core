# Pedestrian impact — reference numbers for Phase 4

What the published literature says about a pedestrian struck by a vehicle, reduced to the numbers DragDoll's tests and
default parameters need (ADR-0024, ADR-0025; workplan task 4.2). Clean provenance only (ADR-0014): every line names
its source and how far it was verified.

**Verification tags**
* **[read]** — taken from the source's full text, read for this document (2026-11-16).
* **[abstract]** — from the source's abstract or official summary only.
* **[via X]** — reported by source X about another work; the original was not read.
* **[ours]** — DragDoll's own choice or derivation; not a literature value.

Units: SI unless stated. Speeds in km/h are vehicle impact speeds.

---

## 1. Trajectory types

| Type | What it is | Source |
|---|---|---|
| Wrap | Struck below the centre of gravity; the body rotates onto the bonnet, the head strikes bonnet or windscreen. The most common configuration for an adult and a car. | Simms 2013 [read]; Wood et al. 2005 [abstract] |
| Forward projection | Centre of gravity within the primary impact zone (pedestrian short relative to the bonnet leading edge: children, high fronts); little rotation; severe mid-body loading. | Simms 2013 [read]; Wood et al. 2005 [abstract] |
| Fender vault, roof vault, somersault | The other three categories of the five-way classification. | Ravani et al. 1981 [via a later conference paper's summary; abstract confirms that trajectory depends on the vehicle / pedestrian configuration, impact speed and deceleration] |

* Front shape decides how much energy the first impact transfers: a wedge front hits the calf and starts the wrap,
  passing on only part of the energy; a pontoon front hits an adult between mid-calf and knee and passes on
  considerably more. Burg & Moser 2007 [via Saulić et al. 2020, read].
* Launch: struck below the centre of gravity -> launched upward at an angle; struck above it -> launched
  horizontally. Saulić et al. 2020 [read].
* Dominant factors overall: shape and speed of the vehicle; height, walking phase and speed of the pedestrian.
  Simms 2013 [read].
* Light trucks and vans: fatality odds about 1.5 times those of cars; fatality risk rises steeply above about
  40 km/h. Simms 2013 [read] (citing Rosén & Sander).

**Not yet sourced:** speed and braking conditions for fender vault, roof vault and somersault; how often each type
occurs. Expected in Ravani et al. 1981 and Wood et al. 2005 (see §7).

## 2. The sequence on the vehicle (wrap)

* Legs first, then a second contact where the head strikes the bonnet or windscreen. A 40 km/h cadaver wrap is shown
  in frames at 0, 40, 80 and 140 ms. Simms 2013 [read].
* Phase 1 has two sub-phases: primary impact -> head / torso contact; then **carried on the bonnet** until separation.
  Soica & Tarulescu [via Saulić et al. 2020, read].
* **Head strike time** (first contact to head contact), 57 reconstructed real collisions (39 sedans, 18 SUV / MPV):
  **50-140 ms in 77 % of cases**; SUV / MPV earlier than sedans (p < 0.05). Li G. et al. 2021 [read, abstract and
  results].
* **Where the head strikes:** bonnet 38.6 %, windscreen 61.4 %. Li G. et al. 2021 [read].
* **Wrap-around distance** of the head: **1.5-2.3 m, mean 1.84 m**; about 74 % within 1.6-2.0 m. Sample: adults,
  mean height 157 cm (women) and 170 cm (men). Li G. et al. 2021 [read].
* **Head contact speed:** on average **83 % (± 23 %) of the vehicle's impact speed**; 67 % of cases between 26 and
  45 km/h. Contact angle below horizontal: mean 53 deg, 70 % of cases between 40 and 70 deg. Li G. et al. 2021 [read].
* A bumper striking at or above the knee is considerably more damaging than below it. Simms 2013 [read].

Caution: Li G. et al. 2021 is internally inconsistent on two secondary points (how head strike time varies with impact
speed; windscreen vs bonnet contact angle). Those are not used here.

* **Cadaver tests, small hatchback, 40 km/h, side-on, walking posture** (5 subjects, 1.54-1.79 m, 47-88 kg; Renault
  Mégane 2004; maximum braking 10 ms after first contact): head wrap-around distance 2.00 / 1.70 / 2.25 / 2.02 / 1.92 m;
  head impact speed relative to the vehicle 11.0 / 8.7 / <1 / 14.5 / 11.8 m/s (the <1 m/s subject's upper arm propped up
  the trunk and prevented a real head strike); head contact about 138 ms after first contact (the matched body model;
  the subjects close to it); pelvis slid 30-45 cm over the bonnet before head contact; elbow contact 37-58 ms before
  head contact; shoulder-to-head 23 ms at most. What governs it: pelvis height relative to the bonnet leading edge
  (pelvic sliding, which increases WAD and head speed), stature, mass, and how much the near upper arm supports the
  trunk. Paas, Östh & Davidsson 2015 [read]; Paas 2015 thesis [read].
* **Contact timeline with a real braking car** (small sedan, 40 km/h, lateral walking stance, braking 10 ms after first
  contact; FSR02): pelvis on the bonnet leading edge 19 ms (WAD 0.90 m), elbow 74 ms (1.64 m), shoulder 114 ms (1.78 m),
  head 120 ms (2.00 m; windscreen crush 85 mm). Three earlier tests (30 / 40 / 40 km/h): pelvis 35 / 38 / 25 ms, elbow
  117 / 104 / 111, shoulder 150 / 134 / 129, head 161 / 142 / 148 ms. Spine and head start moving at 12-16 ms, before
  the pelvis contact; the thorax stays nearly upright until ~60 ms; the near elbow props the thorax and turns the head
  toward the car. **Throw: 11.2 m forward, 1.8 m sideways** (centre of gravity) - the only measured throw of a
  cadaver by a real braking car found so far. Paas et al. 2012 (IRC-12-79) [read].
* **Generic sedan buck, 40 km/h** (3 cadavers, mid-gait, lateral): head contact 135-141 ms, WAD 2.175-2.200 m; head
  x-axis angular velocity > 40 rad/s before head contact (45-54 rad/s in earlier sedan tests); peak head velocity
  relative to the vehicle above the impact speed in all tests; upper bumper takes the first and largest force
  (5.3-5.5 kN), the leading edge loads later through the pelvis rotating **down onto** it; the chest meets mainly the
  windscreen; the shoulder **pockets into the windscreen** and the lower body keeps moving, buckling the spine.
  Forman et al. 2015 (Stapp 59) [read]. A simplified steel-tube buck at 8 m/s (4 cadavers): corridors to 120 ms, plots only.
  Song et al. 2017 (IRC-17-25) [read].
* **What cadaver tests cannot show:** in all of them (production-vehicle bucks on sleds, generic bucks) the subject is
  caught by a net or belts after ~120-250 ms; separation, flight and landing are not measured - except the FSR02 throw
  above. 17 such cadaver tests (5 vehicles incl. a large SUV, 40 km/h) are summarised for injuries in IRC-12-75 [read].

## 3. Flight and slide

* **Three parts of the throw distance:** `S = s0 + s1 + s2` — carried on the vehicle, flight, slide. Saulić et al.
  2020 [read] (after Eubanks).
* **Flight:** 0.45-0.7 s at 33-49 km/h; the body slows by about 0.75-1.8 m/s² in the air. Burg & Moser 2007 [via
  Saulić et al. 2020, read].
* **Slide:** seven dummy tests at similar speeds: 2.0-5.9 m and 0.71-1.24 s, an average deceleration of
  **7.2-8.2 m/s²** (a friction coefficient of about 0.73-0.84); the deceleration depends on how the body first meets
  the ground. Burg & Moser 2007 [via Saulić et al. 2020, read].
* **Body-on-road friction used in reconstruction:** 0.66, 0.70 and 0.79. Searle 1983 / 1993 [via the abstract of a
  2014 SAE study that applies them].
* **General throw equation** for the centre of gravity, launch speed `u`, launch angle `theta`, launch height `h`,
  ground friction `mu`: `S = s0 + mu h + u^2 (cos theta + mu sin theta)^2 / (2 mu g)`. Saulić et al. 2020 [read,
  eq. 2] (after Han & Brach; Searle).
* The ground contact matters for injury as well as the vehicle contact, especially for children. Simms 2013 [read].
* Height and weight of the pedestrian have a negligible effect on flight and slide distance. Burg & Moser 2007 [via
  Saulić et al. 2020, read].
* Simulated dummies that are lighter and slide with less friction than a human body give throw distances that are too
  short (modelled lower than measured in 14 of 19 cases; average error 21 %). Saulić et al. 2020 [read].

* **Real-world speed vs throw distance** (impact point to rest). Wood, Simms & Walsh 2005 [read, Table 1]: Monte
  Carlo single-segment models validated against 185 real wrap and 21 real forward-projection accidents; bands at the
  0.1 / mean / 99.9 percentile of predicted speed. Wrap `v = C (S - So)^D` with (So, C, D) = (1.9, 2.3, 0.51) /
  (1.6, 4.0, 0.47) / (1.2, 5.3, 0.46); forward projection `v = A sqrt(S) + B` with (A, B) = (2.5, -1.2) / (3.7, -1.5)
  / (4.6, -1.0); `v` in m/s, `S` in m. Mean wrap throw: 3.6 m at 20 km/h, 6.4 at 30, 10.4 at 40, 15.7 at 50, 22.4 at
  60, 40 at 80; band at 40 km/h 6.2-23.8 m. Cases with a short throw at high speed are fender vaults (no full
  engagement), not wraps.
* **Coefficient of retardation** (body on ground, lumping slide, roll and bounce, used in the same equation form as
  Searle's): normal, **mean 0.561, sd 0.101** (Wood & Simms 2000, via Wood et al. 2005 [read]). Searle's 0.7 makes
  speeds too high. Higher than this but still published: reconstruction values 0.66-0.79, dummy slides 0.73-0.84
  (above).
* **Wrap model details** (Wood et al. 2005 [read]): separation after the head / shoulder strike comes from braking
  plus the body's vertical and sideways motion; below ~6 m/s combined speed the body drops back onto the bonnet (no
  vertical launch). Primary-to-secondary impact displacement ~1.3 m at 40 km/h (regression of the single-segment
  model). Restitution `e = 0.12 - 0.006 v` (dummy tests). Braking in 85 % of real cases. Forward projection: the
  feet stay mostly on the ground while the body is pushed and falls over (fall-over replaces flight); contact time
  mean 0.056 s (0.01-0.14, 38 cadaver tests). Adult body: centre of gravity at 0.57 of height, radius of gyration
  0.23 of height, mass 68 +- 16 kg.
* **Car-pedestrian friction** in a published full-vehicle simulation set-up: 0.2 (PC-Crash, with body-road 0.6 and
  restitution 0.1) - a modelling choice, not a measurement. Batista 2008 [read]. Staged braking tests reported
  there: 32 km/h -> 7.9 m; 48 km/h -> 13.4, 17.4, 20.7 m; 64 km/h without braking -> 61.9 m.
* **Empirical fits, real and staged cases** (Happer et al. 2000, 202 wrap / 106 forward-projection cases, via Bhalla et
  al. 2002 [read]): wrap `v = 12.7 sqrt(s) - 2.6`, forward projection `v = 11.4 sqrt(s) - 4.0` (`v` km/h, `s` m):
  wrap 11.3 m at 40 km/h. Best of the formulas tested against multi-body simulation.
* **Multi-body reference runs** (Bhalla et al. 2002 [read]; MADYMO 52-body pedestrian validated for the vehicle phase
  against cadavers; car braking 0.7 g from 40 km/h, standing side-on, pedestrian-vehicle friction 0.5, pedestrian-
  ground 0.7):
  * wrap (adult, small car): vehicle contact ~270-300 ms, flight ends ~1.05 s, rest ~2.5 s; the lower torso first
    dips onto the car, then follows a parabola **upward** and forward;
  * forward projection (child, SUV): contact ~90-100 ms, ground contact at 150 ms; lower torso barely rises;
  * an adult struck by an SUV is the closest adult case to forward projection, yet at higher speeds the lower torso
    still rises by **more than 50 %** of its starting height: "leading edge above the centre of gravity -> forward
    projection" does not always hold;
  * struck from behind: contact up to 950 ms, shorter flight, lower peak (joints give way more in that direction);
  * sensitivities of total throw: vehicle-pedestrian geometry up to 45 %, orientation 28 %, no braking 23 %, ground
    friction 0.7 -> 0.4 up to +212 % (bouncing turns into sliding); adults at 40 km/h 10.3-15.5 m across conditions;
  * a dummy with the right size but 32 kg instead of 77 kg flies 17 % higher and throws 20 % shorter.
  * Searle's projection efficiency (launch / vehicle speed): 0.726 adults, 0.813 children.
* **Other simulation set-ups** (modelling choices): pedestrian-ground 0.6 / pedestrian-car 0.4 (Untaroiu et al. 2008
  [read], after Wood & Simms 2000), head-windscreen contact 115 ms at ~57 km/h in their reconstruction. Walking: 55 %
  of struck pedestrians were walking, 72 % struck on their side (US PCDS); hip height varies 25.6 mm over a gait cycle.
* **Side-swipe and minimal-overlap impacts** (21 videos, 9-39 mph): throw distances far shorter than frontal-impact
  formulas predict (a 33 mph impact thrown as if at ~11 mph); frontal formulas do not apply. Neale et al. 2021 [read].

* **Corridor in use** (ADR-0024 amended 2026-11-17): the equation above at the measured launch angle, over `mu` 0.66-0.84, only when the body really flew; bodies leaving a car often head slightly downward (`cos theta + mu sin theta < 1`).

**Not yet sourced:** the ratio of the body's launch speed to the vehicle's impact speed by trajectory type, launch
angles, and a measured friction between body and bonnet (only modelling choices so far: 0.2, 0.4, 0.5).

## 4. Before the impact: what pedestrians do

* 23 volunteers, 51 simulated accident trials: reactions fall into **accelerate** (run, sprint, jump), **freeze**
  (slow down or stop in fright) and **back out** (step back). Young (18-30): 60 % accelerated. Older (60-75): 38 %
  froze, 40 % ran. PEDPCREACT final report [read]; Soni et al. 2013 [abstract].
* **Reaction time:** mean **501 ms** (young), **721 ms** (older). PEDPCREACT [read]. How the time is measured is not
  stated in the summary.
* **Posture at impact is highly variable whatever the reaction** — except that volunteers who froze "raised their
  arms in a triangle to cover their face and head". PEDPCREACT [read].
* Posture changes the outcome: 52 simulations at 40 km/h with the observed postures gave head injury scores from well
  below to above the standard walking posture's. PEDPCREACT [read].
* 19 young men, 34 trials: backward avoidance, forward avoidance, oblique stepping (a startled response) and no
  reaction. **Getting clear needed at least 1.6 s (backward) or 1.8 s (forward) of time to collision when the vehicle
  was noticed.** Li Q. et al. 2021 [read].
* How fast people move when avoiding: pelvis accelerations of about 6-11 m/s² at the peak; speeds at the pre-crash
  instant about 0.7 m/s backward to 1.5 m/s forward. Li Q. et al. 2021 [read].
* Postures seen just before impact: stepping back — turned about 22 deg toward the vehicle, struck-side knee bent
  about 48 deg, struck-side elbow raised and bent; running — a running posture; startled oblique step — turned to
  face the vehicle with the hands raised toward it. Li Q. et al. 2021 [read].
* Startle: a stereotyped response recruited from the neck downward, of brainstem origin. Brown et al. 1991
  [abstract]. **Not yet sourced:** latencies for the trunk and limbs.

## 5. Protective responses in falls and landings

* Unexpected falls from standing height show a repeatable sequence; when the pelvis hit, the wrists hit at nearly
  the same time (38 ms apart on average). Hsiao & Robinovitch 1998 [abstract]. Both wrists took part in 93 % of
  complete falls [via a later review].
* Arresting a forward fall with the arms: peak wrist force 0.65-1.7 body weights; a deliberate soft landing cut it by
  about 27 % against a natural landing and 40 % against stiff arms; it depends on elbow angle and wrist speed at
  impact. DeGoede & Ashton-Miller 2002 [abstract].
* A simulated character can choose what lands first by changing its moment of inertia in the air, then spread the
  landing over several body parts by rolling. Ha, Ye & Liu 2012 [project page and abstract].

---

## 6. What DragDoll takes from this [ours]

**Tests (replace the provisional numbers in ADR-0024 §5 and the workplan):**

| Check | Pass when | From |
|---|---|---|
| Sedan, adult, 30-50 km/h: class | wrap | §1 |
| Sedan: head strike time | 50-140 ms after first contact | §2 |
| Sedan: head strike place | bonnet or windscreen; wrap-around distance 1.5-2.3 m for a 1.6-1.75 m body (scale with height) | §2 |
| Sedan: head speed at strike | 0.6-1.06 x vehicle speed | §2 |
| Flight time at 33-49 km/h | 0.45-0.7 s | §3 |
| Slide | deceleration 6.5-8.3 m/s² on a dry road | §3 |
| Throw distance after separation | `mu h + u^2 / (2 mu g) <= s1 + s2 <= mu h + u^2 (1 + mu^2) / (2 mu g)` for the measured launch speed `u` and height `h` | §3 (the general equation at its extreme launch angles) |
| High front (leading edge at or above the body's centre of gravity) | forward projection, launched near horizontal | §1 |

**Default parameters:**
* Body-on-road friction: 0.7 (range 0.66-0.84 for tuning).
* Voluntary latency: 0.5 s (0.7 s for an "older" preset). Reflex latency stays a tuning value until sourced.
* *Shield* gets two shapes, both observed: **cover** (arms in a triangle over face and head — the freeze) and
  **fend** (hands toward the vehicle, body turned to face it — the startled step).
* Anticipate starts below about 1.2 s to collision. Above about 1.6-1.8 s a person can get clear, which is the host's
  locomotion, not ours: the v1 exclusion of stepping and diving costs little.

**Consequences for the design:**
* The head strikes 50-140 ms after first contact, and a voluntary reaction takes 500 ms: nothing a victim decides
  after the first contact can happen before the head strike. Whatever protects the head on the bonnet was either
  started before the impact (Anticipate) or is a reflex. This confirms the reaction clock of ADR-0025.
* Carry distance `s0` belongs to the throw distance measured from the impact point; the Searle-type corridor applies
  to flight + slide from the separation point. ADR-0024 §5 corrected accordingly.
* Our body must not be lighter or slipperier than a person, or throws come out short (§3, last point): rig mass and
  road friction are part of the passive baseline gate (task 4.3).

## 7. Still to read (full texts not reachable by automated fetch)
* **Ravani, Brougham & Mason 1981**; **Searle & Searle 1983**; **Simms & Wood 2009** (book) — paywalled.
* PDFs obtained by the founder stay outside the repository (workspace `research/` folder); this file gets the numbers
  and their references, not the PDFs. Read 2026-11-17: Wood et al. 2005, Batista 2008, Neale et al. 2021, Bhalla et al. 2002, Untaroiu et al. 2008 (the
  SafetyNet D3.1 report received with them holds safety statistics, nothing on impact mechanics).

## Sources
* Forman J. L., Joodaki H., Forghani A., Riley P. O., Bollapragada V., Lessley D. J., Overby B., Heltzel S., Kerrigan
  J. R., Crandall J. R., Yarboro S., Weiss D. B. "Whole-body Response for Pedestrian Impact with a Generic Sedan Buck."
  Stapp Car Crash Journal 59, 2015, 401-444.
* Batista M. "A Simple Throw Model for Frontal Vehicle-Pedestrian Collisions." Promet - Traffic & Transportation
  20(6), 2008, 357-368.
* Bhalla K., Montazemi P., Crandall J., Yang J., Liu X., Dokko Y., Takahashi Y., Kikuchi Y., Longhitano D. "Vehicle
  Impact Velocity Prediction from Pedestrian Throw Distance: Trade-offs between Throw Formulae, Crash Simulators, and
  Detailed Multi-body Modeling." IRCOBI Conference, Munich, 2002, 263-276.
* Brown P., Rothwell J. C., Thompson P. D., Britton T. C., Day B. L., Marsden C. D. "New observations on the normal
  auditory startle reflex in man." Brain 114(4), 1991, 1891-1902.
* DeGoede K. M., Ashton-Miller J. A. "Fall arrest strategy affects peak hand impact force in a forward fall." Journal
  of Biomechanics 35(6), 2002, 843-848.
* Ha S., Ye Y., Liu C. K. "Falling and Landing Motion Control for Character Animation." ACM Transactions on Graphics
  31(6), 2012.
* Hsiao E. T., Robinovitch S. N. "Common protective movements govern unexpected falls from standing height." Journal
  of Biomechanics 31(1), 1998, 1-9.
* Li G., Liu J., Li K., Zhao H., Shi L., Zhang S., Nie J. "Realistic Reference for Evaluation of Vehicle Safety
  Focusing on Pedestrian Head Protection Observed From Kinematic Reconstruction of Real-World Collisions." Frontiers
  in Bioengineering and Biotechnology 9:768994, 2021. doi:10.3389/fbioe.2021.768994 (CC BY).
* Li Q., Shang S., Pei X., Wang Q., Zhou Q., Nie B. "Kinetic and Kinematic Features of Pedestrian Avoidance Behavior
  in Motor Vehicle Conflicts." Frontiers in Bioengineering and Biotechnology 9:783003, 2021.
  doi:10.3389/fbioe.2021.783003.
* PEDPCREACT — "Pedestrian pre-crash reactions and their effects on crash outcomes", EU project 252605 (IFSTTAR,
  2010-2012), final report summary, CORDIS.
* Ravani B., Brougham D., Mason R. T. "Pedestrian Post-Impact Kinematics and Injury Patterns." SAE Technical Paper
  811024, 1981.
* Saulić N., Papić Z., Ovcin Z. "Pedestrian Throw Distance Prediction from Vehicle Damage Intensity." Promet —
  Traffic & Transportation 32(3), 2020, 371-382 (open access). Reports Burg H., Moser A., "Handbuch
  Verkehrsunfallrekonstruktion", 2007.
* Searle J. A., Searle A. "The Trajectories of Pedestrians, Motorcycles, Motorcyclists, etc., Following a Road
  Accident." SAE Technical Paper 831622, 1983. Searle J. A., SAE Technical Paper 930659, 1993.
* Simms C. "Pedestrian injury biomechanics, crash safety and device design." Engineers Journal (Engineers Ireland),
  2 May 2013.
* Soni A., Robert T., Rongiéras F., Beillas P. "Observations on Pedestrian Pre-Crash Reactions during Simulated
  Accidents." Stapp Car Crash Journal 57, 2013 (SAE 2013-22-0006).
* Neale W. T., Danaher D., Donaldson A., Smith T. "Pedestrian Impact Analysis of Side-Swipe and Minor Overlap
  Conditions." SAE Technical Paper 2021-01-0881, 2021.
* Paas R., Davidsson J., Masson C., Sander U., Brolin K., Yang J. "Pedestrian Shoulder and Spine Kinematics in
  Full-Scale PMHS Tests for Human Body Model Evaluation." IRCOBI Conference 2012, IRC-12-79.
* Song E., Uriot J., Potier P., Dubois D., Petit P., Trosseille X., Douard R. "Reference PMHS Tests to Assess
  Whole-Body Pedestrian Impact Using a Simplified Generic Vehicle Front-End." IRCOBI Conference 2017, IRC-17-25.
* Kerrigan J. R., Arregui-Dalmases C., Foster J., Crandall J. R., Rizzo A. "Pedestrian Injury Analysis: Field Data
  vs. Laboratory Experiments." IRCOBI Conference 2012, IRC-12-75 (injury summary of 17
  full-scale PMHS tests).
* Paas R., Östh J., Davidsson J. "Which pragmatic finite element human body model scaling technique can most accurately
  predict head impact conditions in pedestrian-car crashes?" IRCOBI Conference 2015, IRC-15-64 (open access, Chalmers
  research repository). Paas R., PhD thesis, Chalmers University of Technology, 2015 (pedestrian shoulder, spine and
  head kinematics; Papers I-IV).
* Untaroiu C. D., Meissner M. U., Crandall J. R., Takahashi Y., Okamoto M., Ito O. "Crash reconstruction of
  pedestrian accidents using optimization techniques." International Journal of Impact Engineering, 2008,
  doi:10.1016/j.ijimpeng.2008.01.012.
* Wood D. P., Simms C. K., Walsh D. G. "Vehicle-pedestrian collisions: validated models for pedestrian impact and
  projection." Proc. IMechE Part D 219(2), 2005, 183-195.
