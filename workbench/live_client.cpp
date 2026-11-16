// SPDX-License-Identifier: MIT
#include "live_client.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

namespace
{
int g_port = 0;
SOCKET g_socket = INVALID_SOCKET;
bool g_connecting = false;
ULONGLONG g_nextAttempt = 0;
std::string g_buffer;
bool g_started = false;

void drop()
{
    if (g_socket != INVALID_SOCKET) closesocket(g_socket);
    g_socket = INVALID_SOCKET;
    g_connecting = false;
    g_buffer.clear();
    g_nextAttempt = GetTickCount64() + 150; // Quick: the game recreates its world on every F7 drop
}

void attempt()
{
    g_socket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    u_long nonBlocking = 1;
    ioctlsocket(g_socket, FIONBIO, &nonBlocking);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(g_port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    connect(g_socket, reinterpret_cast<sockaddr*>(&address), sizeof(address)); // Completes asynchronously
    g_connecting = true;
}
} // namespace

namespace live
{
void start(int port)
{
    WSADATA data;
    g_started = WSAStartup(MAKEWORD(2, 2), &data) == 0;
    g_port = port;
}

std::vector<std::string> poll(bool& connected)
{
    std::vector<std::string> lines;
    connected = false;
    if (!g_started) return lines;
    if (g_socket == INVALID_SOCKET)
    {
        if (GetTickCount64() < g_nextAttempt) return lines;
        attempt();
    }
    if (g_connecting)
    {
        fd_set writable, failed;
        FD_ZERO(&writable);
        FD_ZERO(&failed);
        FD_SET(g_socket, &writable);
        FD_SET(g_socket, &failed);
        timeval zero{0, 0};
        if (select(0, nullptr, &writable, &failed, &zero) <= 0) return lines; // Still connecting
        if (FD_ISSET(g_socket, &failed))
        {
            drop(); // Nobody listening yet: try again in a second
            return lines;
        }
        g_connecting = false;
    }
    char chunk[65536];
    for (;;)
    {
        const int n = recv(g_socket, chunk, sizeof(chunk), 0);
        if (n > 0)
        {
            g_buffer.append(chunk, static_cast<size_t>(n));
            continue;
        }
        if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
        {
            drop(); // The world closed (e.g. the game recreated it): reconnect
            return lines;
        }
        break;
    }
    size_t end;
    while ((end = g_buffer.find('\n')) != std::string::npos)
    {
        lines.push_back(g_buffer.substr(0, end));
        g_buffer.erase(0, end + 1);
    }
    connected = true;
    return lines;
}

void send(const std::string& line)
{
    if (g_socket == INVALID_SOCKET || g_connecting) return;
    const std::string data = line + "\n";
    ::send(g_socket, data.data(), static_cast<int>(data.size()), 0); // Tiny and rare: non-blocking is fine
}

void stop()
{
    drop();
    if (g_started) WSACleanup();
    g_started = false;
}
} // namespace live
