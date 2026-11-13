// SPDX-License-Identifier: MIT
// Command channel of the viewer (Phase 2, task 2.7): a TCP server on 127.0.0.1 only (never reachable from other
// machines), one text command per line, one reply line per command. Lets tools (dragdoll_ctl, an MCP bridge) drive
// the viewer while the human watches. Polled from the frame loop: no threads.
//
// Kept apart from main.cpp because <winsock2.h> clashes with raylib's names.
#pragma once

#include <functional>
#include <string>

namespace commands
{
constexpr int kDefaultPort = 47820;

// false + error if the port cannot be opened (e.g. another viewer has it); the viewer then runs without commands.
bool start(int port, std::string& error);

// Accepts a client and handles every complete line received since the last poll. The handler's return value is sent
// back as one line (newlines in it are replaced by spaces).
void poll(const std::function<std::string(const std::string& line)>& handler);

void stop();
} // namespace commands
