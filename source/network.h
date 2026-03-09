#pragma once
// ─── network.h ─── Multiplayer networking (ENet-based) ──────────────────────
// Architecture:
//   - Host/server: one player hosts, others join
//   - Client: connects to host by IP:port
//   - UDP via ENet for low-latency game state
//   - Reliable channel for chat/file sync, unreliable for game state
//
// Packet types:
//   - CONNECT / DISCONNECT / KICK
//   - PLAYER_STATE (position, rotation, health, etc.)
//   - BULLET_SPAWN / BULLET_HIT
//   - ENEMY_STATE (host-authoritative)
//   - BOMB_SPAWN / EXPLOSION
//   - CRATE_SPAWN / PICKUP
//   - CHAT_MESSAGE
//   - FILE_SYNC_REQUEST / FILE_SYNC_DATA / FILE_SYNC_ACK
//   - GAMEMODE_INFO / GAME_START / GAME_END
//   - MAP_CHANGE / SCOREBOARD
// ─────────────────────────────────────────────────────────────────────────────

#include "vec2.h"
#include "gamemode.h"
#include <cstdint>
#include <string>
#include <vector>
#include <functional>
#include <unordered_map>

// Forward declare ENet types to avoid including enet.h in header
typedef struct _ENetHost ENetHost;
typedef struct _ENetPeer ENetPeer;
typedef struct _ENetEvent ENetEvent;

// ── Packet types ──
enum class NetPacketType : uint8_t {
    // Connection
    Connect         = 0x01,
    Disconnect      = 0x02,
    Kick            = 0x03,
    Ping            = 0x04,
    Pong            = 0x05,

    // Lobby
    LobbyInfo       = 0x10,  // host sends lobby state (players, gamemode, map)
    PlayerJoined    = 0x11,
    PlayerLeft      = 0x12,
    ChatMessage     = 0x13,
    Ready           = 0x14,
    GameStart       = 0x15,
    GameEnd         = 0x16,

    // Game state (unreliable, frequent)
    PlayerState     = 0x20,  // position, rotation, hp, animation frame
    EnemyState      = 0x21,  // host-authoritative enemy positions
    BulletSpawn     = 0x22,
    BulletHit       = 0x23,
    BombSpawn       = 0x24,
    ExplosionSpawn  = 0x25,
    BombOrbit       = 0x29,  // notify others that local player has an orbiting bomb
    CrateSpawn      = 0x26,
    PickupCollect   = 0x27,
    EnemyBulletSpawn = 0x28, // host-authoritative enemy bullet spawn

    // Game events (reliable)
    PlayerDied      = 0x30,
    PlayerRespawn   = 0x31,
    EnemyKilled     = 0x32,
    WaveStart       = 0x33,
    ScoreUpdate     = 0x34,
    MapChange       = 0x35,

    // File sync (reliable)
    FileSyncRequest = 0x40,  // "do you have map X?"
    FileSyncOffer   = 0x41,  // "I have file X, size Y"
    FileSyncData    = 0x42,  // chunk of file data
    FileSyncAck     = 0x43,  // "got it" / "need more"
    FileSyncDone    = 0x44,

    // Config
    GamemodeInfo    = 0x50,
    ConfigSync      = 0x51,
    ModSync         = 0x52,  // host sends enabled mod data to clients
    TeamAssign      = 0x53,  // team assignment for a player
    TeamSelectStart = 0x54,  // host tells clients to enter team selection screen
    AdminKick       = 0x55,  // host kicks a player
    AdminRespawn    = 0x56,  // host forces player respawn
    AdminTeamMove   = 0x57,  // host moves a player to a different team
    LivesUpdate     = 0x58,  // sync remaining lives for a player
    HitRequest      = 0x59,  // client→host: "bullet X hit me for Y damage" (PvP validation)
    PlayerHpSync    = 0x5A,  // host→all: authoritative HP update for a player
    SubPlayerState  = 0x5B,  // local splitscreen sub-player positions (unreliable, frequent)
    LobbyHostTransfer = 0x5C, // lobby-host transfer request (host player -> server)
    LobbyHostChanged  = 0x5D, // server -> all: current lobby host id
    LobbyStartRequest = 0x5E, // lobby host -> server: request game start
};

// ── Network channels ──
enum NetChannel : uint8_t {
    NET_CHAN_RELIABLE   = 0,  // game events, file sync, chat
    NET_CHAN_UNRELIABLE = 1,  // game state updates (position etc.)
    NET_CHAN_COUNT      = 2,
};

// ── Sub-player info (splitscreen additional players on a single client) ──
struct SubPlayerInfo {
    Vec2  pos;
    float rotation = 0;
    float legRotation = 0;
    int   hp = 10;
    int   maxHp = 10;
    int   animFrame = 0;
    int   legFrame = 0;
    bool  moving = false;
    bool  alive = true;
    // Interpolation for smooth remote rendering
    Vec2  prevPos, targetPos;
    float interpT = 0;
    float prevRotation = 0, targetRotation = 0;
    uint32_t lastUpdateTick = 0;
};

// ── Player info on network ──
struct NetPlayer {
    uint8_t     id = 0;
    std::string username = "Player";
    Vec2        pos;
    float       rotation = 0;
    int         hp = 10;
    int         maxHp = 10;
    int         score = 0;
    int         kills = 0;
    int         deaths = 0;
    int         animFrame = 0;
    int         legFrame = 0;
    float       legRotation = 0;
    bool        alive = true;
    bool        ready = false;
    uint8_t     localSubPlayers = 0; // additional local players on this client (0..3)
    bool        moving = false;
    float       speed = 520.0f;
    int8_t      team = -1;         // -1 = no team, 0..3 = team index
    int         lives = -1;        // -1 = infinite, >=0 = remaining lives
    bool        spectating = false; // player exhausted lives, now spectating
    ENetPeer*   peer = nullptr;    // null for local player
    uint32_t    lastUpdateTick = 0;

    // Interpolation for smooth remote rendering
    Vec2  prevPos;
    Vec2  targetPos;
    float interpT = 0;
    float prevRotation = 0;
    float targetRotation = 0;

    // Splitscreen sub-players on this client (rendered remotely)
    std::vector<SubPlayerInfo> subPlayers;
};

// ── File transfer state ──
struct FileTransfer {
    std::string filename;
    std::vector<uint8_t> data;
    uint32_t totalSize = 0;
    uint32_t received = 0;
    bool     complete = false;
    uint8_t  peerId = 0;
};

// ── Network session state ──
enum class NetState : uint8_t {
    Offline,        // not in any session
    Hosting,        // running as host
    Connecting,     // attempting to join
    Connected,      // joined a host's game
    InLobby,        // in pre-game lobby
    InGame,         // game in progress
    Syncing,        // transferring files
};

// ── Lobby info ──
struct LobbyInfo {
    std::string hostName;
    std::string mapName;
    std::string mapFile;
    std::string gamemodeName;
    std::string gamemodeId;
    int         maxPlayers = 8;
    int         currentPlayers = 1;
    bool        inProgress = false;
};

// ── Chat message ──
struct ChatMsg {
    std::string sender;
    std::string text;
    float       timestamp = 0;
};

// ── Main network manager ──
class NetworkManager {
public:
    static NetworkManager& instance();

    // Initialize ENet (call once at startup)
    bool init();
    void shutdown();

    // Host a game
    bool host(uint16_t port = 7777, int maxClients = 7);

    // Join a game
    bool join(const std::string& address, uint16_t port = 7777, const std::string& password = "");

    // Disconnect / close
    void disconnect();

    // Must be called every frame to process packets
    void update(float dt);

    // State queries
    NetState state() const { return state_; }
    bool isHost() const { return isHost_; }
    bool isLobbyHost() const { return state_ != NetState::Offline && localId_ == lobbyHostId_; }
    uint8_t lobbyHostId() const { return lobbyHostId_; }
    void setDedicatedServer(bool dedicated) { dedicatedServer_ = dedicated; }
    bool isDedicatedServer() const { return dedicatedServer_; }
    bool isOnline() const { return state_ != NetState::Offline; }
    bool isInGame() const { return state_ == NetState::InGame; }
    uint8_t localPlayerId() const { return localId_; }

    // Ping / latency
    uint32_t getPing() const;  // round-trip time in ms (clients: to host, host: average to clients)
    uint32_t getPlayerPing(uint8_t playerId) const; // RTT for a specific connected player

    // Player management
    const std::vector<NetPlayer>& players() const { return players_; }
    NetPlayer* localPlayer();
    NetPlayer* findPlayer(uint8_t id);
    int playerCount() const { return (int)players_.size(); }
    void setUsername(const std::string& name) { username_ = name; }
    const std::string& username() const { return username_; }
    void setHostPassword(const std::string& pw) { hostPassword_ = pw; }

    // Lobby
    const LobbyInfo& lobbyInfo() const { return lobby_; }
    void setReady(bool ready);
    void setLocalSubPlayers(uint8_t count);
    void setGamemode(const std::string& gamemodeId);
    void setMap(const std::string& mapFile, const std::string& mapName);
    void startGame(uint32_t mapSeed, int mapW, int mapH, const std::vector<uint8_t>& customMapData = {});
    void requestStartGame();
    void sendLobbyHostTransfer(uint8_t targetId);

    // Chat
    void sendChat(const std::string& message);
    const std::vector<ChatMsg>& chatHistory() const { return chat_; }

    // Game state sending (called by game update)
    void sendPlayerState(const NetPlayer& state);
    void sendSubPlayerStates(uint8_t localId, const SubPlayerInfo* subs, int count);
    void sendBulletSpawn(Vec2 pos, float angle, uint8_t playerId, uint32_t netId = 0);
    void sendBulletHit(uint32_t bulletNetId);
    void sendBombSpawn(Vec2 pos, Vec2 vel, uint8_t playerId);
    void sendBombOrbit(uint8_t ownerId);            // broadcast "I have an orbiting bomb"
    void sendExplosion(Vec2 pos, uint8_t ownerId = 255);
    void sendEnemyStates(const void* enemyData, int count);   // host only
    void sendCrateSpawn(Vec2 pos, uint8_t upgradeType);       // host only
    void sendPickupCollect(Vec2 pos, uint8_t upgradeType, uint8_t playerId);
    void sendEnemyBulletSpawn(Vec2 pos, Vec2 dir);            // host only
    void sendPlayerDied(uint8_t playerId, uint8_t killerId);
    void sendPlayerRespawn(uint8_t playerId, Vec2 pos);
    void sendEnemyKilled(uint32_t enemyIdx, uint8_t killerId);
    // PvP host-authoritative hit validation
    void sendHitRequest(uint32_t bulletNetId, int damage, uint8_t ownerId); // client→host
    void sendPlayerHpSync(uint8_t playerId, int hp, int maxHp, uint8_t killerId); // host→all
    void sendWaveStart(int waveNum);
    void sendScoreUpdate(uint8_t playerId, int score);

    // Mod sync (host sends all enabled mod data before game start)
    void sendModSync(const std::vector<uint8_t>& modData);
    void sendConfigSync(const LobbySettings& settings);
    void sendTeamAssignment(uint8_t playerId, int8_t team);
    void sendTeamSelectStart(int teamCount);  // host announces team selection
    void sendAdminKick(uint8_t targetId);     // host only
    void sendAdminRespawn(uint8_t targetId);  // host only
    void sendAdminTeamMove(uint8_t targetId, int8_t newTeam); // host only
    void sendLivesUpdate(uint8_t playerId, int lives); // host only
    void sendGameEnd(uint8_t reason = 0);  // host only — ends game; reason: 0=HostEnded,1=WavesCleared,2=TeamWiped,3=LastAlive,4=TimeUp

    // File sync
    void requestFile(const std::string& filename, uint8_t fromPeer = 0);
    void offerFile(const std::string& filename, const std::vector<uint8_t>& data, uint8_t toPeer);
    bool hasPendingTransfers() const { return !transfers_.empty(); }
    float syncProgress() const;  // 0..1

    // Event callbacks (game.cpp hooks into these)
    std::function<void(uint8_t reason)> onGameEnded; // reason byte same as sendGameEnd
    std::function<void(uint8_t id, const std::string& name)> onPlayerJoined;
    std::function<void(uint8_t id)> onPlayerLeft;
    std::function<void(const NetPlayer& state)> onPlayerStateReceived;
    std::function<void(Vec2 pos, float angle, uint8_t playerId, uint32_t netId)> onBulletSpawned;
    std::function<void(uint32_t netId)> onBulletRemoved;
    std::function<void(Vec2 pos, Vec2 vel, uint8_t playerId)> onBombSpawned;
    std::function<void(uint8_t ownerId)> onBombOrbit;
    std::function<void(Vec2 pos, uint8_t ownerId)> onExplosionSpawned;
    std::function<void(Vec2 pos, uint8_t upgradeType)> onCrateSpawned;
    std::function<void(Vec2 pos, uint8_t upgradeType, uint8_t playerId)> onPickupCollected;
    std::function<void(const void* data, int count)> onEnemyStatesReceived;
    std::function<void(Vec2 pos, Vec2 dir)> onEnemyBulletSpawned;
    std::function<void(uint32_t enemyIdx, uint8_t killerId)> onEnemyKilled;
    std::function<void(const LobbySettings& settings)> onConfigSyncReceived;
    std::function<void(uint8_t playerId, int8_t team)> onTeamAssigned;
    std::function<void(int teamCount)> onTeamSelectStarted;
    std::function<void(uint8_t targetId)> onAdminKicked;
    std::function<void(uint8_t targetId)> onAdminRespawned;
    std::function<void(uint8_t targetId, int8_t newTeam)> onAdminTeamMoved;
    std::function<void(uint8_t playerId, int lives)> onLivesUpdated;
    std::function<void(uint8_t playerId, uint8_t killerId)> onPlayerDied;
    std::function<void(uint8_t playerId, Vec2 pos)> onPlayerRespawned;
    // PvP: host sends authoritative HP; called on all peers including host
    std::function<void(uint8_t playerId, int hp, int maxHp, uint8_t killerId)> onPlayerHpSync;
    // PvP: host receives a hit request from a client; return true to accept
    std::function<bool(uint32_t bulletNetId, int damage, uint8_t ownerId, uint8_t senderPlayerId)> onHitRequest;
    std::function<void(int waveNum)> onWaveStarted;
    std::function<void(const std::string& sender, const std::string& text)> onChatMessage;
    std::function<void(uint32_t mapSeed, int mapW, int mapH, const std::vector<uint8_t>& customMapData)> onGameStarted;
    std::function<void()> onLobbyStartRequested;
    std::function<void(uint8_t newHostId)> onLobbyHostChanged;
    // (onGameEnded declared above — intentional duplicate removed)
    std::function<void(const std::vector<uint8_t>& modData)> onModSyncReceived;
    std::function<void(const std::string& filename)> onFileSyncComplete;
    std::function<void()> onAllSyncsComplete;

private:
    NetworkManager() = default;

    // ENet state
    ENetHost*   enetHost_ = nullptr;
    bool        isHost_ = false;
    NetState    state_ = NetState::Offline;
    std::string username_ = "Player";
    uint8_t     localId_ = 0;
    uint8_t     nextPlayerId_ = 1;
    uint8_t     lobbyHostId_ = 0;
    bool        dedicatedServer_ = false;
    uint32_t    tick_ = 0;
    std::string hostPassword_;          // password required to join (host side, empty=open)
    std::string pendingJoinPassword_;   // password to send in Connect packet (client side)

    // Players
    std::vector<NetPlayer> players_;
    LobbyInfo lobby_;
    std::vector<ChatMsg> chat_;

    // File transfers
    std::vector<FileTransfer> transfers_;

    // Packet handling
    void processEvent(ENetEvent& event);
    void handlePacket(uint8_t* data, size_t len, ENetPeer* from);
    void sendReliable(const std::vector<uint8_t>& data, ENetPeer* peer = nullptr);    // null = broadcast
    void sendUnreliable(const std::vector<uint8_t>& data, ENetPeer* peer = nullptr);

    // Serialization helpers
    std::vector<uint8_t> serializePlayerState(const NetPlayer& p);
    void deserializePlayerState(const uint8_t* data, size_t len, NetPlayer& out);
    std::vector<uint8_t> buildPacket(NetPacketType type, const void* payload, size_t len);
    void updateLobbyHostName();
    void assignLobbyHost(uint8_t newHostId, bool broadcast);
    void broadcastLobbyHostChanged();
};
