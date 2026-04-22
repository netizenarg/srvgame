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

    // Message types
    enum MessageType : uint16_t {
        MESSAGE_TYPE_INVALID = 0,

        // System messages
        MESSAGE_TYPE_HEARTBEAT = 1,
        MESSAGE_TYPE_PROTOCOL_NEGOTIATION = 2,
        MESSAGE_TYPE_AUTHENTICATION = 3,
        MESSAGE_TYPE_ERROR = 4,
        MESSAGE_TYPE_SUCCESS = 5,
        MESSAGE_TYPE_COLLISION_CHECK = 50,

        // World messages
        MESSAGE_TYPE_CHUNK_DATA = 100,
        MESSAGE_TYPE_CHUNK_REQUEST = 101,
        MESSAGE_TYPE_TERRAIN_HEIGHT = 102,
        MESSAGE_TYPE_BIOME_DATA = 103,

        // Player messages
        MESSAGE_TYPE_PLAYER_POSITION = 200,
        MESSAGE_TYPE_PLAYER_VELOCITY = 201,
        MESSAGE_TYPE_PLAYER_ROTATION = 202,
        MESSAGE_TYPE_PLAYER_STATE = 203,
        MESSAGE_TYPE_PLAYER_POSITION_CORRECTION = 204,
        MESSAGE_TYPE_PLAYER_UPDATE = 205,
        MESSAGE_TYPE_PLAYER_SPAWN   = 206,
        MESSAGE_TYPE_PLAYER_DESPAWN = 207,

        // Entity messages
        MESSAGE_TYPE_ENTITY_SPAWN = 300,
        MESSAGE_TYPE_ENTITY_UPDATE = 301,
        MESSAGE_TYPE_ENTITY_DESPAWN = 302,
        MESSAGE_TYPE_ENTITY_BATCH_UPDATE = 303,

        // NPC messages
        MESSAGE_TYPE_NPC_SPAWN = 400,
        MESSAGE_TYPE_NPC_UPDATE = 401,
        MESSAGE_TYPE_NPC_DESPAWN = 402,
        MESSAGE_TYPE_NPC_INTERACTION = 403,

        // Combat messages
        MESSAGE_TYPE_COMBAT_EVENT = 500,
        MESSAGE_TYPE_DAMAGE_EVENT = 501,
        MESSAGE_TYPE_HEALTH_UPDATE = 502,

        // Inventory messages
        MESSAGE_TYPE_LOOT_SPAWN = 600,
        MESSAGE_TYPE_LOOT_PICKUP = 601,
        MESSAGE_TYPE_INVENTORY_UPDATE = 602,
        MESSAGE_TYPE_INVENTORY_MOVE = 603,

        // Chat messages
        MESSAGE_TYPE_CHAT_MESSAGE = 700,
        MESSAGE_TYPE_SYSTEM_MESSAGE = 701,

        // Familiar
        MESSAGE_TYPE_FAMILIAR_COMMAND = 800,

        // Custom messages
        MESSAGE_TYPE_CUSTOM_EVENT = 1000
    };

    // Protocol flags
    enum ProtocolFlags : uint8_t {
        FLAG_COMPRESSED = 0x01,
        FLAG_ENCRYPTED = 0x02,
        FLAG_RELIABLE = 0x04,
        FLAG_ORDERED = 0x08,
        FLAG_PRIORITY_HIGH = 0x10,
        FLAG_PRIORITY_LOW = 0x20
    };

    // Network header
    struct NetworkHeader {
        uint8_t version;           // Protocol version
        uint8_t flags;             // Protocol flags
        uint16_t message_type;     // Message type
        uint32_t sequence;         // Sequence number
        uint32_t timestamp;        // Timestamp in ms
        uint32_t length;           // Payload length
        uint32_t checksum;         // CRC32 checksum

        // Constructor for easy initialization
        NetworkHeader(uint16_t type = 0, uint32_t seq = 0, uint8_t ver = 1, uint8_t flgs = 0)
            : version(ver), flags(flgs), message_type(type),
              sequence(seq), timestamp(0), length(0), checksum(0) {}
    };

    // Message structure
    struct BinaryMessage {
        NetworkHeader header;
        std::vector<uint8_t> data;

        // Serialization
        std::vector<uint8_t> Serialize() const;
        static BinaryMessage Deserialize(const uint8_t* buffer, size_t length);

        // Helper methods
        bool IsCompressed() const { return (header.flags & FLAG_COMPRESSED) != 0; }
        bool IsEncrypted() const { return (header.flags & FLAG_ENCRYPTED) != 0; }
        bool IsReliable() const { return (header.flags & FLAG_RELIABLE) != 0; }
    };

    // Serialization helpers for common types
    class BinaryWriter {
    public:
        BinaryWriter();

        // Write methods
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

        // Get the buffer
        const std::vector<uint8_t>& GetBuffer() const { return buffer_; }
        size_t GetSize() const { return buffer_.size(); }

        // Clear the buffer
        void Clear();

    private:
        std::vector<uint8_t> buffer_;
    };

    class BinaryReader {
    public:
        BinaryReader(const uint8_t* data, size_t length);

        // Read methods
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

        // Check remaining data
        size_t Remaining() const { return length_ - position_; }
        bool CanRead(size_t size) const { return position_ + size <= length_; }

        // Get current position
        size_t GetPosition() const { return position_; }

    private:
        const uint8_t* data_;
        size_t length_;
        size_t position_{0};

        void CheckBounds(size_t size) const;
    };

    // Utility functions
    uint32_t CalculateCRC32(const void* data, size_t length);
    std::vector<uint8_t> CompressData(const std::vector<uint8_t>& data, int level = 6);
    std::vector<uint8_t> DecompressData(const std::vector<uint8_t>& compressed);

    // Protocol version management
    constexpr uint8_t CURRENT_PROTOCOL_VERSION = 1;
    constexpr uint32_t MAX_MESSAGE_SIZE = 10 * 1024 * 1024; // 10 MB

    // Protocol negotiation
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
