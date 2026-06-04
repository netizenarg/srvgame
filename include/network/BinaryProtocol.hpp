#pragma once

#include <cstring>
#include <cstdint>
#include <vector>
#include <stdexcept>
#include <string>
#include <zlib.h>

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#include <nlohmann/json.hpp>

namespace BinaryProtocol {

    enum MessageType : uint16_t {
        MESSAGE_TYPE_INVALID = 0,

        MESSAGE_TYPE_LOG = 1,
        MESSAGE_TYPE_IPC_CLIENT_FORWARD = 2, // worker -> master
        MESSAGE_TYPE_IPC_MASTER_REPLY   = 3, // master -> worker
        MESSAGE_TYPE_IPC_WORKER_READY   = 4, // worker startup
        MESSAGE_TYPE_IPC_SHUTDOWN       = 5,  // master -> worker

        // System messages
        MESSAGE_TYPE_HEARTBEAT = 100,
        MESSAGE_TYPE_PROTOCOL_NEGOTIATION = 102,
        MESSAGE_TYPE_AUTHENTICATION = 103,
        MESSAGE_TYPE_ERROR = 104,
        MESSAGE_TYPE_SUCCESS = 105,
        MESSAGE_TYPE_COLLISION_CHECK = 150,

        // World messages
        MESSAGE_TYPE_CHUNK_PARAMS = 200,
        MESSAGE_TYPE_CHUNK_DATA = 201,
        MESSAGE_TYPE_BIOME_DATA = 202,

        // Player messages
        MESSAGE_TYPE_PLAYER_CONNECT = 300,
        MESSAGE_TYPE_PLAYER_DISCONNECT = 301,
        MESSAGE_TYPE_PLAYER_STATE = 302,
        MESSAGE_TYPE_PLAYER_SPAWN   = 303,
        MESSAGE_TYPE_PLAYER_DESPAWN = 304,
        MESSAGE_TYPE_PLAYER_UPDATE = 305,
        MESSAGE_TYPE_PLAYER_VELOCITY = 306,
        MESSAGE_TYPE_PLAYER_ROTATION = 307,
        MESSAGE_TYPE_PLAYER_POSITION = 308,
        MESSAGE_TYPE_PLAYER_POSITION_CORRECTION = 309,
        // Players messages
        MESSAGE_TYPE_PLAYERS_UPDATE = 350,

        // Entity messages
        MESSAGE_TYPE_ENTITY_SPAWN = 400,
        MESSAGE_TYPE_ENTITY_UPDATE = 401,
        MESSAGE_TYPE_ENTITY_DESPAWN = 402,
        MESSAGE_TYPE_ENTITY_BATCH_UPDATE = 403,

        // NPC messages
        MESSAGE_TYPE_NPC_SPAWN = 500,
        MESSAGE_TYPE_NPC_UPDATE = 501,
        MESSAGE_TYPE_NPC_DESPAWN = 502,
        MESSAGE_TYPE_NPC_INTERACTION = 503,

        // Combat messages
        MESSAGE_TYPE_COMBAT_EVENT = 600,
        MESSAGE_TYPE_DAMAGE_EVENT = 601,
        MESSAGE_TYPE_HEALTH_UPDATE = 602,

        // Inventory messages
        MESSAGE_TYPE_LOOT_SPAWN = 700,
        MESSAGE_TYPE_LOOT_PICKUP = 701,
        MESSAGE_TYPE_INVENTORY_UPDATE = 702,
        MESSAGE_TYPE_INVENTORY_MOVE = 703,

        // Chat messages
        MESSAGE_TYPE_CHAT_MESSAGE = 800,
        MESSAGE_TYPE_SYSTEM_MESSAGE = 801,

        // Familiar
        MESSAGE_TYPE_FAMILIAR_COMMAND = 900,

        // Custom messages
        MESSAGE_TYPE_CUSTOM_EVENT = 1000
    };

    enum ProtocolFlags : uint8_t {
        FLAG_COMPRESSED = 0x01,
        FLAG_ENCRYPTED = 0x02,
        FLAG_RELIABLE = 0x04,
        FLAG_ORDERED = 0x08,
        FLAG_PRIORITY_HIGH = 0x10,
        FLAG_PRIORITY_LOW = 0x20
    };

    struct NetworkHeader {
        uint8_t version;
        uint8_t flags;
        uint16_t message_type;
        uint32_t sequence;
        uint32_t timestamp;
        uint32_t length;
        uint32_t checksum;
        NetworkHeader(uint16_t type = 0, uint32_t seq = 0, uint8_t ver = 1, uint8_t flgs = 0);
    };

    struct BinaryMessage {
        NetworkHeader header;
        std::vector<uint8_t> data;
        std::vector<uint8_t> Serialize() const;
        static BinaryMessage Deserialize(const uint8_t* buffer, size_t length);
        bool IsCompressed() const;
        bool IsEncrypted() const;
        bool IsReliable() const;
    };

    class BinaryWriter {
    public:
        BinaryWriter();
        void WriteRaw(const uint8_t* data, size_t length);
        void WriteUInt8(uint8_t value);
        void WriteUInt16(uint16_t value);
        void WriteUInt32(uint32_t value);
        void WriteUInt64(uint64_t value);
        void WriteInt32(int32_t value);
        void WriteInt64(int64_t value);
        void WriteFloat(float value);
        void WriteDouble(double value);
        void WriteString(const std::string& value);
        void WriteBytes(const uint8_t* data, size_t length);
        void WriteVector3(const glm::vec3& vec);
        void WriteQuaternion(const glm::quat& quaternion);
        void WriteJson(const nlohmann::json& json);
        const std::vector<uint8_t>& GetBuffer() const;
        size_t GetSize() const;
        void Clear();

    private:
        std::vector<uint8_t> buffer_;
    };

    class BinaryReader {
    public:
        BinaryReader(const uint8_t* data, size_t length);
        uint8_t ReadUInt8();
        uint16_t ReadUInt16();
        uint32_t ReadUInt32();
        uint64_t ReadUInt64();
        int32_t ReadInt32();
        int64_t ReadInt64();
        float ReadFloat();
        double ReadDouble();
        std::string ReadString();
        std::vector<uint8_t> ReadBytes(size_t length);
        glm::vec3 ReadVector3();
        glm::quat ReadQuaternion();
        nlohmann::json ReadJson();
        size_t Remaining() const;
        bool CanRead(size_t size) const;
        size_t GetPosition() const;

    private:
        const uint8_t* data_;
        size_t length_;
        size_t position_{0};

        void CheckBounds(size_t size) const;
    };

    uint32_t CalculateCRC32(const void* data, size_t length);
    std::vector<uint8_t> CompressData(const std::vector<uint8_t>& data, int level = 6);
    std::vector<uint8_t> DecompressData(const std::vector<uint8_t>& compressed);

    constexpr uint8_t CURRENT_PROTOCOL_VERSION = 1;
    constexpr uint32_t MAX_MESSAGE_SIZE = 10 * 1024 * 1024;

    struct ProtocolCapabilities {
        uint8_t version;
        bool supports_compression;
        bool supports_encryption;
        uint32_t max_message_size;
        std::vector<uint16_t> supported_message_types;

        std::vector<uint8_t> Serialize() const;
        static ProtocolCapabilities Deserialize(const uint8_t* data, size_t length);
    };

} // namespace BinaryProtocol
