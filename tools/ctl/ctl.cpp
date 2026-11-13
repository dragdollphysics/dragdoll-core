// SPDX-License-Identifier: MIT
// dragdoll_ctl — sends one command to a running dragdoll_view (127.0.0.1) and prints its JSON reply.
//   dragdoll_ctl help
//   dragdoll_ctl load hit_sedan_40kmh
//   dragdoll_ctl step 120
//   dragdoll_ctl state
//   dragdoll_ctl --port 47821 state
// Exit code: 0 = ok, 1 = the viewer answered with an error, 2 = no viewer / connection problem.

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

int main(int argc, char** argv)
{
    int port = 47820;
    std::string line;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc)
        {
            port = std::atoi(argv[++i]);
            continue;
        }
        if (!line.empty()) line += ' ';
        line += argv[i];
    }
    if (line.empty())
    {
        line = "help";
    }

    WSADATA data;
    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
    {
        return 2;
    }
    SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(static_cast<u_short>(port));
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    if (s == INVALID_SOCKET || connect(s, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0)
    {
        std::fprintf(stderr, "no dragdoll_view listening on 127.0.0.1:%d (start it with scripts/watch.ps1)\n", port);
        WSACleanup();
        return 2;
    }
    DWORD timeout = 30000; // A long "step" can take a while
    setsockopt(s, SOL_SOCKET, SO_RCVTIMEO, reinterpret_cast<const char*>(&timeout), sizeof(timeout));

    line += '\n';
    send(s, line.data(), static_cast<int>(line.size()), 0);
    std::string reply;
    char buffer[4096];
    while (reply.find('\n') == std::string::npos)
    {
        const int n = recv(s, buffer, sizeof(buffer), 0);
        if (n <= 0)
        {
            break;
        }
        reply.append(buffer, static_cast<size_t>(n));
    }
    closesocket(s);
    WSACleanup();
    if (reply.empty())
    {
        std::fprintf(stderr, "no reply (viewer busy or closed)\n");
        return 2;
    }
    std::fputs(reply.c_str(), stdout);
    return reply.find("\"ok\":true") != std::string::npos ? 0 : 1;
}
