// Real GameNetworkingSockets transport at the untrusted boundary. One process
// runs a listen socket and a client connection over 127.0.0.1 and exchanges a
// replication-shaped 33-byte frame, so the selected transport library is
// linked and exercised rather than assumed. Elisa still owns replication,
// authority, and policy; this probe only proves the byte boundary moves the
// engine's frame shape unchanged.
#include "probe_core.h"

#include "steam/steamnetworkingsockets.h"
#include "steam/steamnetworkingtypes.h"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>

namespace {

struct ServerState {
    ISteamNetworkingSockets* sockets = nullptr;
    HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
    bool accepted = false;
};

ServerState* g_server = nullptr;

void OnConnectionStatusChanged(SteamNetConnectionStatusChangedCallback_t* info) {
    switch (info->m_info.m_eState) {
        case k_ESteamNetworkingConnectionState_Connecting:
            if (g_server != nullptr and g_server->sockets != nullptr) {
                if (g_server->sockets->AcceptConnection(info->m_hConn) == k_EResultOK) {
                    g_server->connection = info->m_hConn;
                    g_server->accepted = true;
                } else {
                    g_server->sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
                }
            }
            break;
        case k_ESteamNetworkingConnectionState_ClosedByPeer:
        case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
            if (info->m_hConn != k_HSteamNetConnection_Invalid and g_server != nullptr
                    and g_server->sockets != nullptr) {
                g_server->sockets->CloseConnection(info->m_hConn, 0, nullptr, false);
            }
            break;
        default:
            break;
    }
}

// The engine's wire frame: persistent, biased x, biased y, revision, owner,
// all little-endian. Keep this layout in step with src/net/wire.elisa.
void build_frame(uint8_t frame[33], uint64_t persistent, int64_t x, int64_t y, uint64_t revision, uint8_t owner) {
    const uint64_t bias = 1000000;
    const uint64_t x_bits = (uint64_t)(x + (int64_t)bias);
    const uint64_t y_bits = (uint64_t)(y + (int64_t)bias);
    for (int index = 0; index < 8; ++index) {
        frame[index] = (uint8_t)(persistent >> (8 * index));
        frame[8 + index] = (uint8_t)(x_bits >> (8 * index));
        frame[16 + index] = (uint8_t)(y_bits >> (8 * index));
        frame[24 + index] = (uint8_t)(revision >> (8 * index));
    }
    frame[32] = owner;
}

bool deadline_passed(std::chrono::steady_clock::time_point deadline) {
    return std::chrono::steady_clock::now() > deadline;
}

} // namespace

int main() {
    using namespace probe;
    SteamDatagramErrMsg error_message;
    if (!check_named("gns", GameNetworkingSockets_Init(nullptr, error_message), "GameNetworkingSockets initializes")) {
        return 1;
    }
    ISteamNetworkingSockets* sockets = SteamNetworkingSockets();
    if (!check_named("gns", sockets != nullptr, "the default sockets interface is available")) {
        GameNetworkingSockets_Kill();
        return 1;
    }

    ServerState server;
    server.sockets = sockets;
    g_server = &server;

    SteamNetworkingIPAddr local_address;
    local_address.Clear();
    SteamNetworkingConfigValue_t server_options;
    server_options.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, (void*)OnConnectionStatusChanged);
    // GNS refuses port 0, so try a small unprivileged range instead of asking
    // the OS for an ephemeral port.
    HSteamListenSocket listen_socket = k_HSteamListenSocket_Invalid;
    for (uint16_t candidate = 27131; candidate < 27161 and listen_socket == k_HSteamListenSocket_Invalid; ++candidate) {
        local_address.SetIPv4(0x7f000001, candidate);
        listen_socket = sockets->CreateListenSocketIP(local_address, 1, &server_options);
    }
    if (!check_named("gns", listen_socket != k_HSteamListenSocket_Invalid, "GNS listen socket created")) {
        GameNetworkingSockets_Kill();
        return 1;
    }
    SteamNetworkingIPAddr bound_address;
    if (!check_named("gns", sockets->GetListenSocketAddress(listen_socket, &bound_address), "GNS bound address readable")) {
        sockets->CloseListenSocket(listen_socket);
        GameNetworkingSockets_Kill();
        return 1;
    }
    bound_address.SetIPv4(0x7f000001, bound_address.m_port);

    HSteamNetConnection client = sockets->ConnectByIPAddress(bound_address, 0, nullptr);
    if (!check_named("gns", client != k_HSteamNetConnection_Invalid, "GNS client connection requested")) {
        sockets->CloseListenSocket(listen_socket);
        GameNetworkingSockets_Kill();
        return 1;
    }

    const auto connect_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    bool client_connected = false;
    while (!deadline_passed(connect_deadline)) {
        sockets->RunCallbacks();
        SteamNetConnectionInfo_t client_info;
        if (sockets->GetConnectionInfo(client, &client_info)
                && client_info.m_eState == k_ESteamNetworkingConnectionState_Connected) {
            client_connected = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (!check_named("gns", client_connected and server.accepted, "GNS client connects and the server accepts")) {
        sockets->CloseConnection(client, 0, nullptr, false);
        sockets->CloseListenSocket(listen_socket);
        GameNetworkingSockets_Kill();
        return 1;
    }

    uint8_t sent[33];
    build_frame(sent, 7, 3, -2, 42, 0);
    const EResult sent_result = sockets->SendMessageToConnection(
        client, sent, sizeof(sent), k_nSteamNetworkingSend_Reliable, nullptr);
    if (!check_named("gns", sent_result == k_EResultOK, "GNS reliable send accepted")) {
        sockets->CloseConnection(client, 0, nullptr, false);
        sockets->CloseListenSocket(listen_socket);
        GameNetworkingSockets_Kill();
        return 1;
    }

    int delivered = 0;
    uint8_t received[33] = {0};
    const auto receive_deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!deadline_passed(receive_deadline) and delivered == 0) {
        sockets->RunCallbacks();
        SteamNetworkingMessage_t* message = nullptr;
        const int count = sockets->ReceiveMessagesOnConnection(server.connection, &message, 1);
        if (count > 0 and message != nullptr) {
            if (message->m_cbSize == (int)sizeof(received)) {
                std::memcpy(received, message->m_pData, sizeof(received));
                delivered = 1;
            }
            message->Release();
        }
        if (delivered == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    sockets->CloseConnection(client, 0, nullptr, false);
    sockets->CloseConnection(server.connection, 0, nullptr, false);
    sockets->CloseListenSocket(listen_socket);
    GameNetworkingSockets_Kill();
    g_server = nullptr;

    if (!check_named("gns", delivered == 1, "the replication frame arrives on the server connection")) {
        return 1;
    }
    if (!check_named("gns", std::memcmp(sent, received, sizeof(sent)) == 0, "the delivered frame bytes are unchanged")) {
        return 1;
    }
    std::fprintf(stdout, "gns transport: port=%u framed_bytes=%u delivered=%d first=0x%02X\n",
        (unsigned)bound_address.m_port, (unsigned)sizeof(sent), delivered, (unsigned)received[0]);
    return 0;
}
