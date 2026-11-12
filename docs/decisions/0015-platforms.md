# ADR-0015: Win32 x86 is first-class

- **Status:** Accepted
- **Date:** 2026-11-12

## Context
GTA San Andreas is a 32-bit x86 executable; plugins are ASI DLLs.

## Decision
CI builds and tests **Win32 x86 and x64** from day one. The SA plugin DLL links the CRT statically (`/MT`) and has no external runtime dependencies. WASM is deferred until after v1.

## Consequences
32-bit issues surface in week 1, not month 6. Golden hashes exist for both architectures.

## Sources
workplan D15
