// SPDX-License-Identifier: MIT
#include "command_server.hpp"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <algorithm>

namespace
{
SOCKET g_listener = INVALID_SOCKET;
SOCKET g_client = INVALID_SOCKET;
std::string g_pending; // Received bytes not yet ending in a newline

void closeClient()
{
    if (g_client != INVALID_SOCKET)
    {
        closesocket(g_client);
        g_client = INVALID_SOCKET;
    }
    g_pending.clear();
}

void sendAll(const std::string& text)
{
    size_t sent = 0;
    while (sent < text.size())
    {
        const int n = send(g_client, text.data() + sent, static_cast<int>(text.size() - sent), 0);
        if (n == SOCKET_ERROR)
        {
            if (WSAGetLastError() == WSAEWOULDBLOCK)
            {
                Sleep(1); // Local socket: drains almost at once
                continue;
            }
            closeClient();
            return;
        }
        sent += static_cast<size_t>(n);
    }
}
} // namespace

namespace commands
{
bool start(int port, std::string& error)
{
    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        error = "WSAStartup failed";
        return false;
    }
    g_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // 127.0.0.1 only
    BOOL exclusive = TRUE;
    setsockopt(g_listener, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, reinterpret_cast<const char*>(&exclusive), sizeof(exclusive));
    u_long nonBlocking = 1;
    if (g_listener == INVALID_SOCKET || bind(g_listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0 ||
        listen(g_listener, 1) != 0 || ioctlsocket(g_listener, FIONBIO, &nonBlocking) != 0)
    {
        error = "port " + std::to_string(port) + " is not available (another viewer running?)";
        stop();
        return false;
    }
    return true;
}

void poll(const std::function<std::string(const std::string& line)>& handler)
{
    if (g_listener == INVALID_SOCKET)
    {
        return;
    }
    if (g_client == INVALID_SOCKET)
    {
        g_client = accept(g_listener, nullptr, nullptr);
        if (g_client == INVALID_SOCKET)
        {
            return;
        }
        u_long nonBlocking = 1;
        ioctlsocket(g_client, FIONBIO, &nonBlocking);
    }
    char buffer[4096];
    for (;;)
    {
        const int n = recv(g_client, buffer, sizeof(buffer), 0);
        if (n > 0)
        {
            g_pending.append(buffer, static_cast<size_t>(n));
            if (g_pending.size() > 65536)
            {
                closeClient(); // Not a well-behaved client
                return;
            }
            continue;
        }
        if (n == 0 || WSAGetLastError() != WSAEWOULDBLOCK)
        {
            closeClient(); // Client finished or failed; the next one can connect
            return;
        }
        break;
    }
    size_t end;
    while (g_client != INVALID_SOCKET && (end = g_pending.find('\n')) != std::string::npos)
    {
        std::string line = g_pending.substr(0, end);
        g_pending.erase(0, end + 1);
        if (!line.empty() && line.back() == '\r') line.pop_back();
        std::string reply = handler(line);
        std::replace(reply.begin(), reply.end(), '\n', ' ');
        sendAll(reply + "\n");
    }
}

void stop()
{
    closeClient();
    if (g_listener != INVALID_SOCKET)
    {
        closesocket(g_listener);
        g_listener = INVALID_SOCKET;
    }
    WSACleanup();
}
} // namespace commands
