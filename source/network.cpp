// ─── network.cpp ─── ENet-based multiplayer networking ──────────────────────
#include "network.h"
#include "gamemode.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <fstream>

// Networking: ENet for multiplayer
// On Switch: use bundled ENet + libnx BSD sockets (source/enet/)
// On PC:     use system libenet (detected by CMakeLists; HAS_ENET passed in)
#if defined(__SWITCH__)
#  include <enet/enet.h>
#  include <switch.h>
#  ifndef HAS_ENET
#    define HAS_ENET 1
#  endif
#elif __has_include(<enet/enet.h>)
#  include <enet/enet.h>
#  ifndef HAS_ENET
#    define HAS_ENET 1
#  endif
#else
#  ifndef HAS_ENET
#    define HAS_ENET 0
#  endif
#endif

static constexpr uint16_t DEFAULT_PORT = 7777;
static constexpr int MAX_CHANNELS = NET_CHAN_COUNT;
static constexpr uint32_t CONNECT_TIMEOUT = 5000;
static constexpr size_t FILE_CHUNK_SIZE = 4096;
static constexpr float STATE_SEND_RATE = 1.0f / 30.0f; // 30 Hz

NetworkManager& NetworkManager::instance() {
    static NetworkManager mgr;
    return mgr;
}

bool NetworkManager::init() {
#if HAS_ENET
#ifdef __SWITCH__
    // Initialize BSD socket service required by ENet on Switch
    Result rc = socketInitializeDefault();
    if (R_FAILED(rc)) {
        printf("Network: socketInitializeDefault failed: 0x%x\n", rc);
        return false;
    }
    printf("Network: Switch socket service initialized\n");
#endif
    if (enet_initialize() != 0) {
        printf("Network: Failed to init ENet\n");
        return false;
    }
    printf("Network: ENet initialized\n");
    return true;
#else
    printf("Network: Not available on this platform\n");
    return false;
#endif
}

void NetworkManager::shutdown() {
    disconnect();
#if HAS_ENET
    enet_deinitialize();
#ifdef __SWITCH__
    socketExit();
#endif
#endif
}

bool NetworkManager::host(uint16_t port, int maxClients) {
#if HAS_ENET
    if (enetHost_) disconnect();

    ENetAddress address;
    address.host = ENET_HOST_ANY;
    address.port = port;

    enetHost_ = enet_host_create(&address, maxClients, MAX_CHANNELS, 0, 0);
    if (!enetHost_) {
        printf("Network: Failed to create host on port %d\n", port);
        return false;
    }

    isHost_ = true;
    localId_ = 0;
    nextPlayerId_ = 1;
    state_ = NetState::InLobby;

    // Add local player
    NetPlayer local;
    local.id = 0;
    local.username = username_;
    local.ready = false;
    local.peer = nullptr;
    players_.clear();
    players_.push_back(local);

    lobby_.hostName = username_;
    lobby_.currentPlayers = 1;
    lobby_.maxPlayers = maxClients + 1;

    printf("Network: Hosting on port %d (max %d clients)\n", port, maxClients);
    return true;
#else
    return false;
#endif
}

bool NetworkManager::join(const std::string& address, uint16_t port) {
#if HAS_ENET
    if (enetHost_) disconnect();

    enetHost_ = enet_host_create(nullptr, 1, MAX_CHANNELS, 0, 0);
    if (!enetHost_) {
        printf("Network: Failed to create client\n");
        return false;
    }

    ENetAddress addr;
    enet_address_set_host(&addr, address.c_str());
    addr.port = port;

    ENetPeer* peer = enet_host_connect(enetHost_, &addr, MAX_CHANNELS, 0);
    if (!peer) {
        printf("Network: Failed to connect to %s:%d\n", address.c_str(), port);
        enet_host_destroy(enetHost_);
        enetHost_ = nullptr;
        return false;
    }

    isHost_ = false;
    state_ = NetState::Connecting;
    printf("Network: Connecting to %s:%d...\n", address.c_str(), port);
    return true;
#else
    return false;
#endif
}

void NetworkManager::disconnect() {
#if HAS_ENET
    if (!enetHost_) return;

    // Disconnect all peers
    if (isHost_) {
        for (auto& p : players_) {
            if (p.peer) enet_peer_disconnect(p.peer, 0);
        }
    } else {
        // Gracefully disconnect from host
        for (size_t i = 0; i < enetHost_->peerCount; i++) {
            if (enetHost_->peers[i].state == ENET_PEER_STATE_CONNECTED) {
                enet_peer_disconnect(&enetHost_->peers[i], 0);
            }
        }
    }

    // Flush and destroy
    enet_host_flush(enetHost_);
    enet_host_destroy(enetHost_);
    enetHost_ = nullptr;
#endif

    players_.clear();
    transfers_.clear();
    chat_.clear();
    state_ = NetState::Offline;
    isHost_ = false;
    tick_ = 0;
    printf("Network: Disconnected\n");
}

void NetworkManager::update(float dt) {
#if HAS_ENET
    if (!enetHost_) return;

    tick_++;

    ENetEvent event;
    while (enet_host_service(enetHost_, &event, 0) > 0) {
        processEvent(event);
    }

    // Interpolate remote players
    for (auto& p : players_) {
        if (p.id != localId_) {
            // Remote player — interpolate (applies on both host and clients)
            p.interpT += dt * 15.0f; // smooth over ~66ms
            if (p.interpT > 1.0f) p.interpT = 1.0f;
            p.pos = Vec2::lerp(p.prevPos, p.targetPos, p.interpT);
            p.rotation = p.prevRotation + (p.targetRotation - p.prevRotation) * p.interpT;
        }
    }

    // Update shield timers etc for local player (if needed)
#endif
}

#if HAS_ENET
void NetworkManager::processEvent(ENetEvent& event) {
    switch (event.type) {
    case ENET_EVENT_TYPE_CONNECT: {
        if (isHost_) {
            // New client connected
            uint8_t newId = nextPlayerId_++;
            NetPlayer np;
            np.id = newId;
            np.peer = event.peer;
            np.username = "Player " + std::to_string(newId);
            event.peer->data = (void*)(uintptr_t)newId;
            players_.push_back(np);
            lobby_.currentPlayers = (int)players_.size();

            printf("Network: Player %d connected\n", newId);

            // Send lobby info to new player (serialized)
            {
                std::vector<uint8_t> payload;
                // [assignedId:1][hostNameLen:1][hostName][mapNameLen:1][mapName]
                // [mapFileLen:1][mapFile][gamemodeNameLen:1][gamemodeName]
                // [gamemodeIdLen:1][gamemodeId][maxPlayers:1][currentPlayers:1][inProgress:1]
                payload.push_back(newId); // assigned player ID
                payload.push_back((uint8_t)lobby_.hostName.size());
                payload.insert(payload.end(), lobby_.hostName.begin(), lobby_.hostName.end());
                payload.push_back((uint8_t)lobby_.mapName.size());
                payload.insert(payload.end(), lobby_.mapName.begin(), lobby_.mapName.end());
                payload.push_back((uint8_t)lobby_.mapFile.size());
                payload.insert(payload.end(), lobby_.mapFile.begin(), lobby_.mapFile.end());
                payload.push_back((uint8_t)lobby_.gamemodeName.size());
                payload.insert(payload.end(), lobby_.gamemodeName.begin(), lobby_.gamemodeName.end());
                payload.push_back((uint8_t)lobby_.gamemodeId.size());
                payload.insert(payload.end(), lobby_.gamemodeId.begin(), lobby_.gamemodeId.end());
                payload.push_back((uint8_t)lobby_.maxPlayers);
                payload.push_back((uint8_t)lobby_.currentPlayers);
                payload.push_back(lobby_.inProgress ? 1 : 0);
                auto pkt = buildPacket(NetPacketType::LobbyInfo, payload.data(), payload.size());
                sendReliable(pkt, event.peer);
            }

            // Notify existing players about the new player
            {
                std::vector<uint8_t> joinPayload;
                joinPayload.push_back(newId);
                joinPayload.push_back((uint8_t)np.username.size());
                joinPayload.insert(joinPayload.end(), np.username.begin(), np.username.end());
                auto jpkt = buildPacket(NetPacketType::PlayerJoined, joinPayload.data(), joinPayload.size());
                for (auto& p : players_) {
                    if (p.peer && p.peer != event.peer) sendReliable(jpkt, p.peer);
                }
            }

            // Send info about all existing players to the new client
            // (so the new client knows about everyone already in the lobby)
            for (auto& p : players_) {
                if (p.id != newId && p.id != 0) { // skip new player and host (host sent in LobbyInfo)
                    std::vector<uint8_t> existPayload;
                    existPayload.push_back(p.id);
                    existPayload.push_back((uint8_t)p.username.size());
                    existPayload.insert(existPayload.end(), p.username.begin(), p.username.end());
                    auto epkt = buildPacket(NetPacketType::PlayerJoined, existPayload.data(), existPayload.size());
                    sendReliable(epkt, event.peer);
                }
            }

            if (onPlayerJoined) onPlayerJoined(newId, np.username);
        } else {
            // We connected to host
            state_ = NetState::InLobby;
            printf("Network: Connected to host\n");

            // Send our username
            auto pkt = buildPacket(NetPacketType::Connect,
                username_.c_str(), username_.size() + 1);
            sendReliable(pkt, event.peer);
        }
        break;
    }

    case ENET_EVENT_TYPE_DISCONNECT: {
        uint8_t peerId = (uint8_t)(uintptr_t)event.peer->data;
        printf("Network: Player %d disconnected\n", peerId);

        auto it = std::remove_if(players_.begin(), players_.end(),
            [peerId](const NetPlayer& p) { return p.id == peerId; });
        players_.erase(it, players_.end());
        lobby_.currentPlayers = (int)players_.size();

        if (onPlayerLeft) onPlayerLeft(peerId);

        if (!isHost_ && state_ != NetState::Offline) {
            // Lost connection to host
            state_ = NetState::Offline;
            printf("Network: Lost connection to host\n");
        }
        break;
    }

    case ENET_EVENT_TYPE_RECEIVE: {
        if (event.packet->dataLength > 0) {
            handlePacket(event.packet->data, event.packet->dataLength, event.peer);
        }
        enet_packet_destroy(event.packet);
        break;
    }

    default:
        break;
    }
}

void NetworkManager::handlePacket(uint8_t* data, size_t len, ENetPeer* from) {
    if (len < 1) return;
    NetPacketType type = (NetPacketType)data[0];
    uint8_t* payload = data + 1;
    size_t payloadLen = len - 1;

    switch (type) {
    case NetPacketType::Connect: {
        if (isHost_ && payloadLen > 0) {
            // Client sent username
            std::string name((char*)payload, payloadLen - 1);
            uint8_t peerId = (uint8_t)(uintptr_t)from->data;
            if (auto* p = findPlayer(peerId)) {
                p->username = name;
                printf("Network: Player %d is '%s'\n", peerId, name.c_str());
                // Broadcast updated player name to all clients
                std::vector<uint8_t> joinPayload;
                joinPayload.push_back(peerId);
                joinPayload.push_back((uint8_t)name.size());
                joinPayload.insert(joinPayload.end(), name.begin(), name.end());
                auto jpkt = buildPacket(NetPacketType::PlayerJoined, joinPayload.data(), joinPayload.size());
                for (auto& op : players_) {
                    if (op.peer) sendReliable(jpkt, op.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::LobbyInfo: {
        if (!isHost_ && payloadLen > 5) {
            // Deserialize lobby info from host
            size_t off = 0;
            localId_ = payload[off++];
            printf("Network: Assigned player ID %d\n", localId_);

            // Add ourselves to the player list
            players_.clear();
            NetPlayer local;
            local.id = localId_;
            local.username = username_;
            local.peer = nullptr;
            players_.push_back(local);

            // Add host as player 0
            NetPlayer hostP;
            hostP.id = 0;
            uint8_t hostNameLen = payload[off++];
            hostP.username = std::string((char*)payload + off, hostNameLen); off += hostNameLen;
            hostP.peer = &from->host->peers[0]; // not used directly, just marks as remote
            players_.insert(players_.begin(), hostP);

            uint8_t mapNameLen = payload[off++];
            lobby_.mapName = std::string((char*)payload + off, mapNameLen); off += mapNameLen;
            uint8_t mapFileLen = payload[off++];
            lobby_.mapFile = std::string((char*)payload + off, mapFileLen); off += mapFileLen;
            uint8_t gmNameLen = payload[off++];
            lobby_.gamemodeName = std::string((char*)payload + off, gmNameLen); off += gmNameLen;
            uint8_t gmIdLen = payload[off++];
            lobby_.gamemodeId = std::string((char*)payload + off, gmIdLen); off += gmIdLen;
            lobby_.maxPlayers = payload[off++];
            lobby_.currentPlayers = payload[off++];
            lobby_.inProgress = payload[off++] != 0;
            lobby_.hostName = hostP.username;

            state_ = NetState::InLobby;
            printf("Network: Lobby info received (host=%s, gamemode=%s)\n",
                   lobby_.hostName.c_str(), lobby_.gamemodeName.c_str());
        }
        break;
    }

    case NetPacketType::PlayerJoined: {
        if (payloadLen >= 2) {
            uint8_t pid = payload[0];
            uint8_t nameLen = payload[1];
            std::string name = (payloadLen > 2 + nameLen) ?
                std::string((char*)payload + 2, nameLen) :
                std::string((char*)payload + 2, payloadLen - 2);
            // Don't duplicate
            bool exists = false;
            for (auto& p : players_) {
                if (p.id == pid) { p.username = name; exists = true; break; }
            }
            if (!exists) {
                NetPlayer np;
                np.id = pid;
                np.username = name;
                players_.push_back(np);
            }
            lobby_.currentPlayers = (int)players_.size();
            if (onPlayerJoined) onPlayerJoined(pid, name);
        }
        break;
    }

    case NetPacketType::PlayerState: {
        if (payloadLen >= 35) {
            uint8_t pid = payload[0];
            NetPlayer state;
            memcpy(&state.pos.x,      payload + 1, 4);
            memcpy(&state.pos.y,      payload + 5, 4);
            memcpy(&state.rotation,   payload + 9, 4);
            memcpy(&state.legRotation,payload + 13, 4);
            memcpy(&state.hp,         payload + 17, 4);
            memcpy(&state.animFrame,  payload + 21, 4);
            memcpy(&state.legFrame,   payload + 25, 4);
            state.id = pid;
            state.moving = (payload[29] != 0);
            state.alive  = (payload[30] != 0);
            memcpy(&state.maxHp,      payload + 31, 4);

            if (auto* p = findPlayer(pid)) {
                // First state update: snap to position instead of interpolating from (0,0)
                if (p->lastUpdateTick == 0) {
                    p->prevPos = state.pos;
                    p->targetPos = state.pos;
                    p->pos = state.pos;
                    p->prevRotation = state.rotation;
                    p->targetRotation = state.rotation;
                } else {
                    p->prevPos = p->targetPos;
                    p->prevRotation = p->targetRotation;
                    p->targetPos = state.pos;
                    p->targetRotation = state.rotation;
                }
                p->interpT = 0;
                p->hp = state.hp;
                p->maxHp = state.maxHp;
                p->animFrame = state.animFrame;
                p->legFrame = state.legFrame;
                p->legRotation = state.legRotation;
                p->moving = state.moving;
                p->alive = state.alive;
                p->lastUpdateTick = tick_;
            }

            if (onPlayerStateReceived) onPlayerStateReceived(state);

            // If host, relay to other clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::PlayerState, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) {
                        sendUnreliable(pkt, p.peer);
                    }
                }
            }
        }
        break;
    }

    case NetPacketType::BulletSpawn: {
        if (payloadLen >= 9) {
            Vec2 pos;
            float angle;
            memcpy(&pos.x,  payload, 4);
            memcpy(&pos.y,  payload + 4, 4);
            memcpy(&angle,  payload + 8, 4);
            uint8_t pid   = payloadLen > 12 ? payload[12] : 0;
            uint32_t netId = 0;
            if (payloadLen >= 17) memcpy(&netId, payload + 13, 4);

            if (onBulletSpawned) onBulletSpawned(pos, angle, pid, netId);

            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::BulletSpawn, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::BulletHit: {
        if (payloadLen >= 4) {
            uint32_t netId = 0;
            memcpy(&netId, payload, 4);
            if (onBulletRemoved) onBulletRemoved(netId);
            // Host relays to all clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::BulletHit, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::BombSpawn: {
        if (payloadLen >= 17) {
            Vec2 pos, vel;
            memcpy(&pos.x, payload, 4);
            memcpy(&pos.y, payload + 4, 4);
            memcpy(&vel.x, payload + 8, 4);
            memcpy(&vel.y, payload + 12, 4);
            uint8_t pid = payload[16];
            if (onBombSpawned) onBombSpawned(pos, vel, pid);

            // Host relays bombs to other clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::BombSpawn, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::ExplosionSpawn: {
        if (payloadLen >= 8) {
            Vec2 pos;
            memcpy(&pos.x, payload, 4);
            memcpy(&pos.y, payload + 4, 4);
            if (onExplosionSpawned) onExplosionSpawned(pos);
            // Host relays explosions to all other clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::ExplosionSpawn, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::ChatMessage: {
        if (payloadLen > 1) {
            uint8_t nameLen = payload[0];
            if (payloadLen > 1 + nameLen) {
                std::string sender((char*)payload + 1, nameLen);
                std::string text((char*)payload + 1 + nameLen, payloadLen - 1 - nameLen);

                ChatMsg msg;
                msg.sender = sender;
                msg.text = text;
                chat_.push_back(msg);

                if (onChatMessage) onChatMessage(sender, text);

                // Host relays chat
                if (isHost_) {
                    auto pkt = buildPacket(NetPacketType::ChatMessage, payload, payloadLen);
                    for (auto& p : players_) {
                        if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                    }
                }
            }
        }
        break;
    }

    case NetPacketType::GameStart: {
        if (!isHost_) {
            state_ = NetState::InGame;
            uint32_t mapSeed = 0;
            int32_t mapW = 50, mapH = 50;  // defaults
            if (payloadLen >= 4)  memcpy(&mapSeed, payload, 4);
            if (payloadLen >= 8)  memcpy(&mapW, payload + 4, 4);
            if (payloadLen >= 12) memcpy(&mapH, payload + 8, 4);
            // Custom map data follows the 12-byte header if present
            std::vector<uint8_t> customMapData;
            if (payloadLen > 12) {
                customMapData.assign(payload + 12, payload + payloadLen);
            }
            if (onGameStarted) onGameStarted(mapSeed, mapW, mapH, customMapData);
        }
        break;
    }

    case NetPacketType::GameEnd: {
        state_ = NetState::InLobby;
        if (onGameEnded) onGameEnded();
        break;
    }

    case NetPacketType::PlayerDied: {
        if (payloadLen >= 2) {
            uint8_t pid = payload[0];
            uint8_t killerId = payload[1];
            if (auto* p = findPlayer(pid)) p->alive = false;
            // Credit kill to the attacker
            if (killerId != pid) {
                if (auto* killer = findPlayer(killerId)) {
                    killer->kills++;
                    killer->score += 10;
                }
            }
            if (onPlayerDied) onPlayerDied(pid, killerId);
            // Host relays to all other clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::PlayerDied, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::PlayerRespawn: {
        if (payloadLen >= 9) {
            uint8_t pid = payload[0];
            Vec2 pos;
            memcpy(&pos.x, payload + 1, 4);
            memcpy(&pos.y, payload + 5, 4);
            if (auto* p = findPlayer(pid)) {
                p->alive = true;
                p->pos = pos;
                p->targetPos = pos;
                p->prevPos = pos;
            }
            if (onPlayerRespawned) onPlayerRespawned(pid, pos);
            // Host relays to all other clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::PlayerRespawn, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::WaveStart: {
        if (payloadLen >= 4) {
            int wave;
            memcpy(&wave, payload, 4);
            if (onWaveStarted) onWaveStarted(wave);
            // Host relays to all other clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::WaveStart, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::ScoreUpdate: {
        if (payloadLen >= 5) {
            uint8_t pid = payload[0];
            int score;
            memcpy(&score, payload + 1, 4);
            if (auto* p = findPlayer(pid)) p->score = score;
        }
        break;
    }

    case NetPacketType::CrateSpawn: {
        if (payloadLen >= 9) {
            Vec2 pos;
            memcpy(&pos.x, payload, 4);
            memcpy(&pos.y, payload + 4, 4);
            uint8_t upType = payload[8];
            if (onCrateSpawned) onCrateSpawned(pos, upType);
        }
        break;
    }

    case NetPacketType::PickupCollect: {
        if (payloadLen >= 10) {
            Vec2 pos;
            memcpy(&pos.x, payload, 4);
            memcpy(&pos.y, payload + 4, 4);
            uint8_t upType = payload[8];
            uint8_t pid = payload[9];
            if (onPickupCollected) onPickupCollected(pos, upType, pid);
        }
        break;
    }

    case NetPacketType::FileSyncRequest: {
        if (isHost_ && payloadLen > 0) {
            std::string filename((char*)payload, payloadLen);
            printf("Network: File sync request for '%s'\n", filename.c_str());
            // Read file and send
            std::ifstream file(filename, std::ios::binary);
            if (file.is_open()) {
                std::vector<uint8_t> data((std::istreambuf_iterator<char>(file)),
                                           std::istreambuf_iterator<char>());
                offerFile(filename, data, (uint8_t)(uintptr_t)from->data);
            }
        }
        break;
    }

    case NetPacketType::FileSyncData: {
        if (payloadLen > 5) {
            uint8_t nameLen = payload[0];
            std::string filename((char*)payload + 1, nameLen);
            uint32_t totalSize;
            memcpy(&totalSize, payload + 1 + nameLen, 4);
            size_t dataOffset = 1 + nameLen + 4;
            size_t chunkSize = payloadLen - dataOffset;

            // Find or create transfer
            FileTransfer* tf = nullptr;
            for (auto& t : transfers_) {
                if (t.filename == filename) { tf = &t; break; }
            }
            if (!tf) {
                transfers_.push_back({});
                tf = &transfers_.back();
                tf->filename = filename;
                tf->totalSize = totalSize;
                tf->data.reserve(totalSize);
            }

            tf->data.insert(tf->data.end(), payload + dataOffset, payload + payloadLen);
            tf->received = (uint32_t)tf->data.size();

            if (tf->received >= tf->totalSize) {
                tf->complete = true;
                printf("Network: File '%s' received (%u bytes)\n", filename.c_str(), totalSize);

                // Write to disk
                std::ofstream out(filename, std::ios::binary);
                if (out.is_open()) {
                    out.write((char*)tf->data.data(), tf->data.size());
                }

                if (onFileSyncComplete) onFileSyncComplete(filename);

                // Remove from transfers
                transfers_.erase(
                    std::remove_if(transfers_.begin(), transfers_.end(),
                        [&](const FileTransfer& t) { return t.filename == filename; }),
                    transfers_.end());

                // Check if all transfers done
                if (transfers_.empty() && onAllSyncsComplete) {
                    onAllSyncsComplete();
                }
            }
        }
        break;
    }

    case NetPacketType::Ready: {
        if (isHost_ && payloadLen >= 1) {
            uint8_t peerId = (uint8_t)(uintptr_t)from->data;
            bool ready = payload[0] != 0;
            if (auto* p = findPlayer(peerId)) p->ready = ready;
        }
        break;
    }

    case NetPacketType::EnemyKilled: {
        if (payloadLen >= 5) {
            uint32_t enemyIdx;
            memcpy(&enemyIdx, payload, 4);
            uint8_t killerId = payload[4];
            // Update the killer's score
            if (auto* p = findPlayer(killerId)) p->score += 10;
            // Notify game layer (clients apply enemy death)
            if (onEnemyKilled) onEnemyKilled(enemyIdx, killerId);
            // Host relays to all clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::EnemyKilled, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::MapChange: {
        if (!isHost_ && payloadLen > 0) {
            // Host sent us the map to load
            std::string mapName((char*)payload, payloadLen);
            lobby_.mapFile = mapName;
            printf("Network: Map changed to '%s'\n", mapName.c_str());
        }
        break;
    }

    case NetPacketType::ModSync: {
        if (!isHost_ && payloadLen > 0) {
            // Client received mod sync data from host
            std::vector<uint8_t> modData(payload, payload + payloadLen);
            printf("Network: Received mod sync data (%zu bytes)\n", modData.size());
            if (onModSyncReceived) onModSyncReceived(modData);
        }
        break;
    }

    case NetPacketType::ConfigSync: {
        if (payloadLen >= 26) {
            LobbySettings settings;
            uint8_t flags = payload[0];
            settings.friendlyFire   = (flags & 0x01) != 0;
            settings.pvpEnabled     = (flags & 0x02) != 0;
            settings.upgradesShared = (flags & 0x04) != 0;
            int32_t mw, mh, php;
            memcpy(&mw,  payload + 1, 4);
            memcpy(&mh,  payload + 5, 4);
            memcpy(&settings.enemyHpScale,    payload + 9,  4);
            memcpy(&settings.enemySpeedScale, payload + 13, 4);
            memcpy(&settings.spawnRateScale,  payload + 17, 4);
            memcpy(&php, payload + 21, 4);
            settings.mapWidth     = mw;
            settings.mapHeight    = mh;
            settings.playerMaxHp  = php;
            settings.teamCount    = payload[25];
            // Extended fields (v2, 28 bytes)
            if (payloadLen >= 28) {
                settings.livesPerPlayer = payload[26];
                settings.livesShared    = (payload[27] != 0);
            }
            // Extended fields (v3, 37 bytes)
            if (payloadLen >= 36) {
                settings.isPvp = (payload[28] != 0);
                memcpy(&settings.crateInterval, payload + 29, 4);
                int16_t wc;
                memcpy(&wc, payload + 33, 2);
                settings.waveCount = wc;
                settings.maxPlayers = payload[35];
            }
            if (onConfigSyncReceived) onConfigSyncReceived(settings);
            // Host relays to all clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::ConfigSync, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        } else if (payloadLen >= 1) {
            // Legacy 1-byte format: just pvp flag
            LobbySettings settings;
            settings.pvpEnabled = (payload[0] & 0x01) != 0;
            settings.friendlyFire = settings.pvpEnabled;
            if (onConfigSyncReceived) onConfigSyncReceived(settings);
        }
        break;
    }

    case NetPacketType::TeamAssign: {
        if (payloadLen >= 2) {
            uint8_t pid = payload[0];
            int8_t team = (int8_t)payload[1];
            if (auto* p = findPlayer(pid)) p->team = team;
            if (onTeamAssigned) onTeamAssigned(pid, team);
            // Host relays to all clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::TeamAssign, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::TeamSelectStart: {
        if (payloadLen >= 1) {
            int tc = payload[0];
            if (onTeamSelectStarted) onTeamSelectStarted(tc);
        }
        break;
    }

    case NetPacketType::AdminKick: {
        // Client receives kick — host handles disconnection directly
        if (!isHost_ && payloadLen >= 1) {
            if (onAdminKicked) onAdminKicked(payload[0]);
        }
        break;
    }

    case NetPacketType::AdminRespawn: {
        if (payloadLen >= 1) {
            uint8_t tid = payload[0];
            if (auto* p = findPlayer(tid)) {
                p->alive = true;
                p->hp = p->maxHp;
                p->spectating = false;
                p->lives = -1; // reset infinite on host-forced respawn
            }
            if (onAdminRespawned) onAdminRespawned(tid);
            // Host relays to all clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::AdminRespawn, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::AdminTeamMove: {
        if (payloadLen >= 2) {
            uint8_t tid = payload[0];
            int8_t newTeam = (int8_t)payload[1];
            if (auto* p = findPlayer(tid)) p->team = newTeam;
            if (onAdminTeamMoved) onAdminTeamMoved(tid, newTeam);
            // Host relays
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::AdminTeamMove, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::LivesUpdate: {
        if (payloadLen >= 5) {
            uint8_t pid = payload[0];
            int32_t lv;
            memcpy(&lv, payload + 1, 4);
            if (auto* p = findPlayer(pid)) p->lives = lv;
            if (onLivesUpdated) onLivesUpdated(pid, lv);
            // Host relays to all clients
            if (isHost_) {
                auto pkt = buildPacket(NetPacketType::LivesUpdate, payload, payloadLen);
                for (auto& p : players_) {
                    if (p.peer && p.peer != from) sendReliable(pkt, p.peer);
                }
            }
        }
        break;
    }

    case NetPacketType::EnemyState: {
        // Only clients receive this (host is authoritative)
        if (!isHost_ && payloadLen >= 4) {
            int count;
            memcpy(&count, payload, 4);
            size_t perEnemy = 4 + 4 + 4 + 4 + 1; // x, y, rot, hp, type
            if (count > 0 && payloadLen >= (size_t)(4 + count * perEnemy)) {
                if (onEnemyStatesReceived) {
                    onEnemyStatesReceived(payload + 4, count);
                }
            }
        }
        break;
    }

    default:
        break;
    }
}
#endif

// ── Send helpers ──
void NetworkManager::sendReliable(const std::vector<uint8_t>& data, ENetPeer* peer) {
#if HAS_ENET
    ENetPacket* pkt = enet_packet_create(data.data(), data.size(),
        ENET_PACKET_FLAG_RELIABLE);
    if (peer) {
        enet_peer_send(peer, NET_CHAN_RELIABLE, pkt);
    } else if (isHost_) {
        enet_host_broadcast(enetHost_, NET_CHAN_RELIABLE, pkt);
    } else {
        // Client: send to server peer (first connected peer)
        if (enetHost_) {
            for (size_t i = 0; i < enetHost_->peerCount; i++) {
                if (enetHost_->peers[i].state == ENET_PEER_STATE_CONNECTED) {
                    enet_peer_send(&enetHost_->peers[i], NET_CHAN_RELIABLE, pkt);
                    break;
                }
            }
        }
    }
#endif
}

void NetworkManager::sendUnreliable(const std::vector<uint8_t>& data, ENetPeer* peer) {
#if HAS_ENET
    ENetPacket* pkt = enet_packet_create(data.data(), data.size(), 0);
    if (peer) {
        enet_peer_send(peer, NET_CHAN_UNRELIABLE, pkt);
    } else if (isHost_) {
        enet_host_broadcast(enetHost_, NET_CHAN_UNRELIABLE, pkt);
    } else {
        // Client: send to server peer
        if (enetHost_) {
            for (size_t i = 0; i < enetHost_->peerCount; i++) {
                if (enetHost_->peers[i].state == ENET_PEER_STATE_CONNECTED) {
                    enet_peer_send(&enetHost_->peers[i], NET_CHAN_UNRELIABLE, pkt);
                    break;
                }
            }
        }
    }
#endif
}

std::vector<uint8_t> NetworkManager::buildPacket(NetPacketType type, const void* payload, size_t len) {
    std::vector<uint8_t> pkt;
    pkt.reserve(1 + len);
    pkt.push_back((uint8_t)type);
    if (payload && len > 0) {
        auto* p = (const uint8_t*)payload;
        pkt.insert(pkt.end(), p, p + len);
    }
    return pkt;
}

// ── Player management ──
NetPlayer* NetworkManager::localPlayer() {
    for (auto& p : players_) {
        if (p.id == localId_) return &p;
    }
    return nullptr;
}

NetPlayer* NetworkManager::findPlayer(uint8_t id) {
    for (auto& p : players_) {
        if (p.id == id) return &p;
    }
    return nullptr;
}

// ── Lobby ──
void NetworkManager::setReady(bool ready) {
#if HAS_ENET
    if (isHost_) {
        if (auto* p = localPlayer()) p->ready = ready;
    } else {
        uint8_t val = ready ? 1 : 0;
        auto pkt = buildPacket(NetPacketType::Ready, &val, 1);
        sendReliable(pkt);
    }
#endif
}

void NetworkManager::setGamemode(const std::string& gamemodeId) {
    lobby_.gamemodeId = gamemodeId;
    auto* entry = GameModeRegistry::instance().find(gamemodeId);
    if (entry) {
        lobby_.gamemodeName = entry->displayName;
    }
}

void NetworkManager::setMap(const std::string& mapFile, const std::string& mapName) {
    lobby_.mapFile = mapFile;
    lobby_.mapName = mapName;

    if (isHost_) {
        auto pkt = buildPacket(NetPacketType::MapChange, mapFile.c_str(), mapFile.size());
        sendReliable(pkt);
    }
}

void NetworkManager::startGame(uint32_t mapSeed, int mapW, int mapH, const std::vector<uint8_t>& customMapData) {
#if HAS_ENET
    if (!isHost_) return;
    state_ = NetState::InGame;
    // Payload: 4 bytes seed + 4 bytes w + 4 bytes h + (optional) custom map data
    std::vector<uint8_t> payload(12);
    int32_t w = mapW, h = mapH;
    memcpy(payload.data(),     &mapSeed, 4);
    memcpy(payload.data() + 4, &w, 4);
    memcpy(payload.data() + 8, &h, 4);
    if (!customMapData.empty()) {
        payload.insert(payload.end(), customMapData.begin(), customMapData.end());
    }
    auto pkt = buildPacket(NetPacketType::GameStart, payload.data(), payload.size());
    sendReliable(pkt);
    if (onGameStarted) onGameStarted(mapSeed, mapW, mapH, customMapData);
#endif
}

// ── Chat ──
void NetworkManager::sendChat(const std::string& message) {
#if HAS_ENET
    std::vector<uint8_t> payload;
    payload.push_back((uint8_t)username_.size());
    payload.insert(payload.end(), username_.begin(), username_.end());
    payload.insert(payload.end(), message.begin(), message.end());

    auto pkt = buildPacket(NetPacketType::ChatMessage, payload.data(), payload.size());
    sendReliable(pkt);

    // Add to local chat
    ChatMsg msg;
    msg.sender = username_;
    msg.text = message;
    chat_.push_back(msg);
#endif
}

// ── Game state sending ──
void NetworkManager::sendPlayerState(const NetPlayer& state) {
#if HAS_ENET
    uint8_t payload[35];
    payload[0] = state.id;
    memcpy(payload + 1,  &state.pos.x, 4);
    memcpy(payload + 5,  &state.pos.y, 4);
    memcpy(payload + 9,  &state.rotation, 4);
    memcpy(payload + 13, &state.legRotation, 4);
    memcpy(payload + 17, &state.hp, 4);
    memcpy(payload + 21, &state.animFrame, 4);
    memcpy(payload + 25, &state.legFrame, 4);
    payload[29] = state.moving ? 1 : 0;
    payload[30] = state.alive  ? 1 : 0;
    memcpy(payload + 31, &state.maxHp, 4);

    auto pkt = buildPacket(NetPacketType::PlayerState, payload, 35);
    sendUnreliable(pkt);
#endif
}

void NetworkManager::sendBulletSpawn(Vec2 pos, float angle, uint8_t playerId, uint32_t netId) {
#if HAS_ENET
    uint8_t payload[17];
    memcpy(payload,      &pos.x, 4);
    memcpy(payload + 4,  &pos.y, 4);
    memcpy(payload + 8,  &angle, 4);
    payload[12] = playerId;
    memcpy(payload + 13, &netId, 4);
    auto pkt = buildPacket(NetPacketType::BulletSpawn, payload, 17);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendBulletHit(uint32_t bulletNetId) {
#if HAS_ENET
    auto pkt = buildPacket(NetPacketType::BulletHit, &bulletNetId, 4);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendBombSpawn(Vec2 pos, Vec2 vel, uint8_t playerId) {
#if HAS_ENET
    uint8_t payload[17];
    memcpy(payload, &pos.x, 4);
    memcpy(payload + 4, &pos.y, 4);
    memcpy(payload + 8, &vel.x, 4);
    memcpy(payload + 12, &vel.y, 4);
    payload[16] = playerId;
    auto pkt = buildPacket(NetPacketType::BombSpawn, payload, 17);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendExplosion(Vec2 pos) {
#if HAS_ENET
    uint8_t payload[8];
    memcpy(payload, &pos.x, 4);
    memcpy(payload + 4, &pos.y, 4);
    auto pkt = buildPacket(NetPacketType::ExplosionSpawn, payload, 8);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendEnemyStates(const void* enemyData, int count) {
#if HAS_ENET
    // Batch: [count:4][{pos.x:4, pos.y:4, rot:4, hp:4, type:1}*count]
    size_t perEnemy = 4 + 4 + 4 + 4 + 1;
    std::vector<uint8_t> payload(4 + count * perEnemy);
    memcpy(payload.data(), &count, 4);
    // Caller fills the rest via the raw pointer
    if (enemyData && count > 0) {
        memcpy(payload.data() + 4, enemyData, count * perEnemy);
    }
    auto pkt = buildPacket(NetPacketType::EnemyState, payload.data(), payload.size());
    sendUnreliable(pkt);
#endif
}

void NetworkManager::sendCrateSpawn(Vec2 pos, uint8_t upgradeType) {
#if HAS_ENET
    uint8_t payload[9];
    memcpy(payload, &pos.x, 4);
    memcpy(payload + 4, &pos.y, 4);
    payload[8] = upgradeType;
    auto pkt = buildPacket(NetPacketType::CrateSpawn, payload, 9);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendPickupCollect(Vec2 pos, uint8_t upgradeType, uint8_t playerId) {
#if HAS_ENET
    uint8_t payload[10];
    memcpy(payload, &pos.x, 4);
    memcpy(payload + 4, &pos.y, 4);
    payload[8] = upgradeType;
    payload[9] = playerId;
    auto pkt = buildPacket(NetPacketType::PickupCollect, payload, 10);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendPlayerDied(uint8_t playerId, uint8_t killerId) {
#if HAS_ENET
    uint8_t payload[2] = { playerId, killerId };
    auto pkt = buildPacket(NetPacketType::PlayerDied, payload, 2);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendPlayerRespawn(uint8_t playerId, Vec2 pos) {
#if HAS_ENET
    uint8_t payload[9];
    payload[0] = playerId;
    memcpy(payload + 1, &pos.x, 4);
    memcpy(payload + 5, &pos.y, 4);
    auto pkt = buildPacket(NetPacketType::PlayerRespawn, payload, 9);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendEnemyKilled(uint32_t enemyIdx, uint8_t killerId) {
#if HAS_ENET
    uint8_t payload[5];
    memcpy(payload, &enemyIdx, 4);
    payload[4] = killerId;
    auto pkt = buildPacket(NetPacketType::EnemyKilled, payload, 5);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendWaveStart(int waveNum) {
#if HAS_ENET
    auto pkt = buildPacket(NetPacketType::WaveStart, &waveNum, 4);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendScoreUpdate(uint8_t playerId, int score) {
#if HAS_ENET
    uint8_t payload[5];
    payload[0] = playerId;
    memcpy(payload + 1, &score, 4);
    auto pkt = buildPacket(NetPacketType::ScoreUpdate, payload, 5);
    sendReliable(pkt);
#endif
}

// ── Mod sync ──
void NetworkManager::sendModSync(const std::vector<uint8_t>& modData) {
#if HAS_ENET
    if (!isHost_ || modData.empty()) return;
    auto pkt = buildPacket(NetPacketType::ModSync, modData.data(), modData.size());
    sendReliable(pkt);
    printf("Network: Sent mod sync data (%zu bytes)\n", modData.size());
#endif
}

void NetworkManager::sendConfigSync(const LobbySettings& settings) {
#if HAS_ENET
    // Serialize: flags:1, mapW:4, mapH:4, ehp:4, espd:4, srate:4, playerHp:4, teamCount:1, lives:1, livesShared:1,
    //            isPvp:1, crateInterval:4, waveCount:2, maxPlayers:1 = 37 bytes
    uint8_t payload[37];
    uint8_t flags = 0;
    if (settings.friendlyFire) flags |= 0x01;
    if (settings.pvpEnabled)   flags |= 0x02;
    if (settings.upgradesShared) flags |= 0x04;
    payload[0] = flags;
    int32_t mw = settings.mapWidth, mh = settings.mapHeight;
    memcpy(payload + 1, &mw, 4);
    memcpy(payload + 5, &mh, 4);
    memcpy(payload + 9,  &settings.enemyHpScale, 4);
    memcpy(payload + 13, &settings.enemySpeedScale, 4);
    memcpy(payload + 17, &settings.spawnRateScale, 4);
    int32_t php = settings.playerMaxHp;
    memcpy(payload + 21, &php, 4);
    payload[25] = (uint8_t)settings.teamCount;
    payload[26] = (uint8_t)(settings.livesPerPlayer > 255 ? 255 : settings.livesPerPlayer);
    payload[27] = settings.livesShared ? 1 : 0;
    // v3 extended fields
    payload[28] = settings.isPvp ? 1 : 0;
    memcpy(payload + 29, &settings.crateInterval, 4);
    int16_t wc = (int16_t)(settings.waveCount > 32767 ? 32767 : settings.waveCount);
    memcpy(payload + 33, &wc, 2);
    payload[35] = (uint8_t)settings.maxPlayers;
    payload[36] = 0; // reserved
    auto pkt = buildPacket(NetPacketType::ConfigSync, payload, 37);
    sendReliable(pkt);
    printf("Network: Sent lobby settings sync\n");
#endif
}

void NetworkManager::sendTeamAssignment(uint8_t playerId, int8_t team) {
#if HAS_ENET
    uint8_t payload[2] = { playerId, (uint8_t)team };
    auto pkt = buildPacket(NetPacketType::TeamAssign, payload, 2);
    sendReliable(pkt);
    // Also update locally
    if (auto* p = findPlayer(playerId)) p->team = team;
#endif
}

void NetworkManager::sendTeamSelectStart(int teamCount) {
#if HAS_ENET
    uint8_t payload[1] = { (uint8_t)teamCount };
    auto pkt = buildPacket(NetPacketType::TeamSelectStart, payload, 1);
    sendReliable(pkt);
#endif
}

void NetworkManager::sendAdminKick(uint8_t targetId) {
#if HAS_ENET
    uint8_t payload[1] = { targetId };
    auto pkt = buildPacket(NetPacketType::AdminKick, payload, 1);
    sendReliable(pkt);
    // Disconnect the peer on host
    if (auto* p = findPlayer(targetId)) {
        if (p->peer) p->peer->data = nullptr; // flag for disconnect
        if (onAdminKicked) onAdminKicked(targetId);
    }
#endif
}

void NetworkManager::sendAdminRespawn(uint8_t targetId) {
#if HAS_ENET
    uint8_t payload[1] = { targetId };
    auto pkt = buildPacket(NetPacketType::AdminRespawn, payload, 1);
    sendReliable(pkt);
    if (onAdminRespawned) onAdminRespawned(targetId);
#endif
}

void NetworkManager::sendAdminTeamMove(uint8_t targetId, int8_t newTeam) {
#if HAS_ENET
    uint8_t payload[2] = { targetId, (uint8_t)newTeam };
    auto pkt = buildPacket(NetPacketType::AdminTeamMove, payload, 2);
    sendReliable(pkt);
    if (auto* p = findPlayer(targetId)) p->team = newTeam;
    if (onAdminTeamMoved) onAdminTeamMoved(targetId, newTeam);
#endif
}

void NetworkManager::sendLivesUpdate(uint8_t playerId, int lives) {
#if HAS_ENET
    uint8_t payload[5];
    payload[0] = playerId;
    int32_t lv = lives;
    memcpy(payload + 1, &lv, 4);
    auto pkt = buildPacket(NetPacketType::LivesUpdate, payload, 5);
    sendReliable(pkt);
    if (auto* p = findPlayer(playerId)) p->lives = lives;
    if (onLivesUpdated) onLivesUpdated(playerId, lives);
#endif
}

void NetworkManager::sendGameEnd() {
#if HAS_ENET
    auto pkt = buildPacket(NetPacketType::GameEnd, nullptr, 0);
    sendReliable(pkt);
    state_ = NetState::InLobby;
    if (onGameEnded) onGameEnded();
#endif
}

// ── File sync ──
void NetworkManager::requestFile(const std::string& filename, uint8_t fromPeer) {
#if HAS_ENET
    auto pkt = buildPacket(NetPacketType::FileSyncRequest, filename.c_str(), filename.size());
    if (fromPeer == 0 && !isHost_) {
        // Request from host (first peer)
        if (enetHost_->peerCount > 0) {
            sendReliable(pkt, &enetHost_->peers[0]);
        }
    } else {
        if (auto* p = findPlayer(fromPeer)) {
            if (p->peer) sendReliable(pkt, p->peer);
        }
    }
#endif
}

void NetworkManager::offerFile(const std::string& filename, const std::vector<uint8_t>& data, uint8_t toPeer) {
#if HAS_ENET
    auto* p = findPlayer(toPeer);
    if (!p || !p->peer) return;

    uint32_t totalSize = (uint32_t)data.size();
    size_t offset = 0;

    while (offset < data.size()) {
        size_t chunkSize = std::min(FILE_CHUNK_SIZE, data.size() - offset);

        std::vector<uint8_t> payload;
        payload.push_back((uint8_t)filename.size());
        payload.insert(payload.end(), filename.begin(), filename.end());
        // Total size (for first chunk, receiver uses this to know when done)
        uint8_t sizeBytes[4];
        memcpy(sizeBytes, &totalSize, 4);
        payload.insert(payload.end(), sizeBytes, sizeBytes + 4);
        // Chunk data
        payload.insert(payload.end(), data.begin() + offset, data.begin() + offset + chunkSize);

        auto pkt = buildPacket(NetPacketType::FileSyncData, payload.data(), payload.size());
        sendReliable(pkt, p->peer);

        offset += chunkSize;
    }
#endif
}

float NetworkManager::syncProgress() const {
    if (transfers_.empty()) return 1.0f;
    uint32_t totalNeeded = 0, totalGot = 0;
    for (auto& t : transfers_) {
        totalNeeded += t.totalSize;
        totalGot += t.received;
    }
    return totalNeeded > 0 ? (float)totalGot / (float)totalNeeded : 1.0f;
}

uint32_t NetworkManager::getPing() const {
#if HAS_ENET
    if (!enetHost_) return 0;
    if (!isHost_) {
        // Client: get RTT to host (first peer)
        if (enetHost_->peerCount > 0 && enetHost_->peers[0].state == ENET_PEER_STATE_CONNECTED) {
            return enetHost_->peers[0].roundTripTime;
        }
    } else {
        // Host: average RTT across connected clients
        uint32_t total = 0;
        int count = 0;
        for (auto& p : players_) {
            if (p.peer && p.peer->state == ENET_PEER_STATE_CONNECTED) {
                total += p.peer->roundTripTime;
                count++;
            }
        }
        if (count > 0) return total / count;
    }
#endif
    return 0;
}

uint32_t NetworkManager::getPlayerPing(uint8_t playerId) const {
#if HAS_ENET
    for (auto& p : players_) {
        if (p.id == playerId && p.peer && p.peer->state == ENET_PEER_STATE_CONNECTED) {
            return p.peer->roundTripTime;
        }
    }
    // If we're a client and asking about the host, use our own peer RTT
    if (!isHost_ && playerId == 0 && enetHost_ && enetHost_->peerCount > 0
        && enetHost_->peers[0].state == ENET_PEER_STATE_CONNECTED) {
        return enetHost_->peers[0].roundTripTime;
    }
#endif
    return 0;
}

// ── Serialization helpers ──
std::vector<uint8_t> NetworkManager::serializePlayerState(const NetPlayer& p) {
    std::vector<uint8_t> buf(35);
    buf[0] = p.id;
    memcpy(buf.data() + 1,  &p.pos.x, 4);
    memcpy(buf.data() + 5,  &p.pos.y, 4);
    memcpy(buf.data() + 9,  &p.rotation, 4);
    memcpy(buf.data() + 13, &p.legRotation, 4);
    memcpy(buf.data() + 17, &p.hp, 4);
    memcpy(buf.data() + 21, &p.animFrame, 4);
    memcpy(buf.data() + 25, &p.legFrame, 4);
    buf[29] = p.moving ? 1 : 0;
    buf[30] = p.alive  ? 1 : 0;
    memcpy(buf.data() + 31, &p.maxHp, 4);
    return buf;
}

void NetworkManager::deserializePlayerState(const uint8_t* data, size_t len, NetPlayer& out) {
    if (len < 35) return;
    out.id = data[0];
    memcpy(&out.pos.x,       data + 1, 4);
    memcpy(&out.pos.y,       data + 5, 4);
    memcpy(&out.rotation,    data + 9, 4);
    memcpy(&out.legRotation, data + 13, 4);
    memcpy(&out.hp,          data + 17, 4);
    memcpy(&out.animFrame,   data + 21, 4);
    memcpy(&out.legFrame,    data + 25, 4);
    out.moving = data[29] != 0;
    out.alive  = data[30] != 0;
    memcpy(&out.maxHp,       data + 31, 4);
}
