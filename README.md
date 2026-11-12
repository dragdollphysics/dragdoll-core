# DragDoll

**rag*doll*** + **drag** (ballroom & vogue: poses, dips, death drops) + **drag** (the physics force).

An open-source, real-time **active ragdoll** library in C++20, built on [Jolt Physics](https://github.com/jrouwe/JoltPhysics).

## Why
Games still play the same canned "fall down" clip whether you're hit by a scooter or a bus. DragDoll gives characters a body that *reacts*: it braces, wraps, tumbles, lands and gets back up, driven by physics and behaviours instead of keyframes. Free, open, engine-agnostic, built from published research, and made for modders first.

DragDoll is more than a library: it's a **workflow to replace the canned reaction animations of any game** with an active ragdoll — find the trigger, map the skeleton, mirror the world, override the pose, sync the root, hand back. The first reference integration is GTA San Andreas; the rest belongs to the community.

**DragDoll will never** ship proprietary code or data, require a specific engine, or hide its cheats (any non-physical assist is explicit and off by default).

## Status
🚧 **Pre-alpha: planning done, no code yet.** First goal: a GTA San Andreas mod where DragDoll takes over pedestrians hit by vehicles: pre-impact → impact → wrap / vault / projection → airborne → landing → settle → hand back to the game.

* Plan: [`docs/workplan.md`](docs/workplan.md)
* Decisions: [`docs/decisions/`](docs/decisions/README.md)
* Progress log: [`docs/devlog.md`](docs/devlog.md)

## License
[MIT](LICENSE). Contributions are signed off under the [Developer Certificate of Origin](https://developercertificate.org) (`git commit -s`).

DragDoll is **community-owned**: copyright belongs to all contributors, repositories live under the [dragdollphysics](https://github.com/dragdollphysics) GitHub organization, and governance is described in [ADR-0019](docs/decisions/0019-governance.md).

*DragDoll is an independent project. It is not affiliated with, endorsed by, or derived from any commercial animation system, game, or publisher.*
