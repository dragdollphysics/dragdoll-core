# ADR-0015: Win32 x86 is first-class

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
GTA San Andreas is a 32-bit x86 executable; plugins are ASI DLLs.

## Decision
CI builds and tests **Win32 x86 and x64** from day one. The SA plugin DLL links the CRT statically (`/MT`) and has no external runtime dependencies. WASM is deferred until after v1.

**Amended 2026-11-12 (pre-Phase-1 retrospective) — guest-process budgets:** DragDoll runs inside other programs' processes (SA is 32-bit with little address space left). Memory per world < 64 MB, tracked through Jolt allocator hooks; worker thread count configurable by the host (SA default 2–4); DragDoll never calls host code from its own threads; performance is tracked on x86 as well as x64.

## Consequences
32-bit issues surface in week 1, not month 6. Golden hashes exist for both architectures.

## Sources
workplan D15
