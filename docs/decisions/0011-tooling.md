# ADR-0011: Build and tooling

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
One choice per tool avoids churn.

## Decision
CMake >= 3.24 with **FetchContent** (pinned tags), Ninja, ccache; **MSVC** for Win32 x86 and x64; **doctest**; **GitHub Actions**; **Tracy** profiler; Visual Studio or RAD Debugger for debugging.

## Consequences
Dependencies download into the build tree; no vendored third_party folder.

## Sources
workplan D11
