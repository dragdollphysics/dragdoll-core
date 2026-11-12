# DragDoll

**rag*doll*** + **drag** (ballroom & vogue: poses, dips, death drops) + **drag** (the physics force).

An open-source, real-time **active ragdoll** library in C++20, built on [Jolt Physics](https://github.com/jrouwe/JoltPhysics).

## Why
Games still play the same canned "fall down" clip whether you're hit by a scooter or a bus. DragDoll gives characters a body that *reacts*: it braces, wraps, tumbles, lands and gets back up, driven by physics and behaviours instead of keyframes. Free, open, engine-agnostic, built from published research, and made for modders first.

**DragDoll will never** ship proprietary code or data, require a specific engine, or hide its cheats (any non-physical assist is explicit and off by default).

## Status
🚧 **Pre-alpha: planning done, no code yet.** First goal: a GTA San Andreas mod where DragDoll takes over pedestrians hit by vehicles: pre-impact → impact → wrap / vault / projection → airborne → landing → settle → hand back to the game.

* Plan: [`docs/workplan.md`](docs/workplan.md)
* Decisions: [`docs/decisions/`](docs/decisions/README.md)
* Progress log: [`docs/devlog.md`](docs/devlog.md)

## License
[MIT](LICENSE). Contributions are signed off under the [Developer Certificate of Origin](https://developercertificate.org) (`git commit -s`).

*DragDoll is an independent project. It is not affiliated with, endorsed by, or derived from any commercial animation system, game, or publisher.*
