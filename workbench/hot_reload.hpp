// SPDX-License-Identifier: MIT
// Hot reload of dragdoll.dll in the viewer (Phase 2, task 2.4).
//
// The viewer delay-loads dragdoll.dll (/DELAYLOAD + /DELAY:UNLOAD) and a delay-load hook points it at a shadow copy in
// bin/hot/, so the build can overwrite bin/dragdoll.dll while the viewer runs. When the file changes, the viewer
// releases every DragDoll object, calls dragdoll_shutdown(), then swap(): the old copy is unloaded and the next
// dragdoll_* call loads a fresh copy of the new build.
//
// Kept apart from main.cpp because <windows.h> clashes with raylib's names.
#pragma once

#include <string>

namespace hot
{
// Call before the first dragdoll_* call. false = hot reload unavailable (the library still loads normally).
bool start(std::string& error);

// Polled about twice a second: true once bin/dragdoll.dll has changed and stayed unchanged for one poll
// (the linker has finished writing it).
bool changed();

// Precondition: no DragDoll object alive and dragdoll_shutdown() called. Unloads the old copy and prepares the new one.
bool swap(std::string& error);

// 1 for the library loaded at start, +1 per successful reload.
int generation();
} // namespace hot
