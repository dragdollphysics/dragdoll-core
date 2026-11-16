// SPDX-License-Identifier: MIT
// Viewer side of the live tap (bridge task 3.7): connects to a world streaming on 127.0.0.1:port and hands over
// complete JSON lines. Reconnects by itself (e.g. when the game recreates its world). Polled from the frame loop.
// Kept apart from main.cpp because <winsock2.h> clashes with raylib's names.
#pragma once

#include <string>
#include <vector>

namespace live
{
// Starts connecting to 127.0.0.1:port (retried every ~0.15 s until a world is there).
void start(int port);
// Returns the complete lines received since the last call (may be empty). `connected` tells the link state.
std::vector<std::string> poll(bool& connected);
// Sends one line to the host (e.g. {"t":"show","id":2,"visible":0}). Ignored while not connected.
void send(const std::string& line);
void stop();
} // namespace live
