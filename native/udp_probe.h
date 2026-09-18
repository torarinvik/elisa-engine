#pragma once
// A real transport boundary for the replication frame. The Elisa net modules
// model loss, delay, and convergence over an in-memory loopback; this proves
// the same 33-byte frame shape moves across an actual OS socket. It is a
// stand-in for the GameNetworkingSockets transport the plan selects, so it is
// labelled as such: it shows the byte boundary, not the selected library.
#include "probe_core.h"

#include <arpa/inet.h>
#include <cstring>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace probe {

inline bool probe_udp_loopback() {
    const int handle = ::socket(AF_INET, SOCK_DGRAM, 0);
    if (!check(handle >= 0, "udp socket")) {
        return false;
    }
    sockaddr_in address;
    std::memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    if (!check(::bind(handle, (sockaddr*)&address, sizeof(address)) == 0, "udp bind")) {
        ::close(handle);
        return false;
    }
    socklen_t length = sizeof(address);
    if (!check(::getsockname(handle, (sockaddr*)&address, &length) == 0, "udp getsockname")) {
        ::close(handle);
        return false;
    }
    // 33 bytes matches src/net/wire.elisa's frame size; the payload is
    // arbitrary because this checks the transport, not the encoding.
    unsigned char frame[33];
    for (int index = 0; index < 33; ++index) {
        frame[index] = (unsigned char)(index * 7 + 1);
    }
    const ssize_t sent = ::sendto(handle, frame, sizeof(frame), 0, (sockaddr*)&address, sizeof(address));
    if (!check(sent == (ssize_t)sizeof(frame), "udp send")) {
        ::close(handle);
        return false;
    }
    unsigned char received[64];
    const ssize_t got = ::recvfrom(handle, received, sizeof(received), 0, nullptr, nullptr);
    const unsigned port = (unsigned)ntohs(address.sin_port);
    ::close(handle);
    if (!check(got == (ssize_t)sizeof(frame), "udp receive length")) {
        return false;
    }
    if (!check(std::memcmp(frame, received, sizeof(frame)) == 0, "udp bytes round trip")) {
        return false;
    }
    std::fprintf(stdout, "udp: port=%u bytes=%d round_trip=ok\n", port, (int)sizeof(frame));
    return true;
}

} // namespace probe
