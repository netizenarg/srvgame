#include "network/BinaryProtocol.hpp"

namespace BinaryProtocol {

    NetworkHeader::NetworkHeader(uint16_t type, uint32_t seq, uint8_t ver, uint8_t flgs)
    : version(ver), flags(flgs), message_type(type),
    sequence(seq), timestamp(0), length(0), checksum(0) {}

    std::vector<uint8_t> BinaryMessage::Serialize() const {
        std::vector<uint8_t> buffer(sizeof(NetworkHeader) + data.size());
        memcpy(buffer.data(), &header, sizeof(NetworkHeader));
        if (!data.empty()) {
            memcpy(buffer.data() + sizeof(NetworkHeader), data.data(), data.size());
        }
        return buffer;
    }
    BinaryMessage BinaryMessage::Deserialize(const uint8_t* buffer, size_t length) {
        if (length < sizeof(NetworkHeader)) {
            throw std::runtime_error("Buffer too small for message header");
        }
        BinaryMessage msg;
        memcpy(&msg.header, buffer, sizeof(NetworkHeader));
        if (length != sizeof(NetworkHeader) + msg.header.length) {
            throw std::runtime_error("Message length mismatch");
        }
        if (msg.header.length > 0) {
            msg.data.resize(msg.header.length);
            memcpy(msg.data.data(), buffer + sizeof(NetworkHeader), msg.header.length);
        }
        return msg;
    }
    bool BinaryMessage::IsCompressed() const { return (header.flags & FLAG_COMPRESSED) != 0; }
    bool BinaryMessage::IsEncrypted() const { return (header.flags & FLAG_ENCRYPTED) != 0; }
    bool BinaryMessage::IsReliable() const { return (header.flags & FLAG_RELIABLE) != 0; }


    BinaryWriter::BinaryWriter() { buffer_.reserve(1024); }

    void BinaryWriter::WriteRaw(const uint8_t* data, size_t length) {
        buffer_.insert(buffer_.end(), data, data + length);
    }

    void BinaryWriter::WriteUInt8(uint8_t value) { buffer_.push_back(value); }

    void BinaryWriter::WriteUInt16(uint16_t value) {
        buffer_.push_back(static_cast<uint8_t>(value >> 8));
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void BinaryWriter::WriteUInt32(uint32_t value) {
        buffer_.push_back(static_cast<uint8_t>(value >> 24));
        buffer_.push_back(static_cast<uint8_t>(value >> 16));
        buffer_.push_back(static_cast<uint8_t>(value >> 8));
        buffer_.push_back(static_cast<uint8_t>(value & 0xFF));
    }

    void BinaryWriter::WriteUInt64(uint64_t value) {
        for (int i = 7; i >= 0; --i) {
            buffer_.push_back(static_cast<uint8_t>(value >> (i * 8)));
        }
    }

    void BinaryWriter::WriteInt32(int32_t value) {
        WriteUInt32(static_cast<uint32_t>(value));
    }

    void BinaryWriter::WriteInt64(int64_t value) {
        WriteUInt64(static_cast<uint64_t>(value));
    }

    void BinaryWriter::WriteFloat(float value) {
        uint32_t int_value;
        memcpy(&int_value, &value, sizeof(float));
        WriteUInt32(int_value);
    }

    void BinaryWriter::WriteDouble(double value) {
        uint64_t int_value;
        memcpy(&int_value, &value, sizeof(double));
        WriteUInt64(int_value);
    }

    void BinaryWriter::WriteString(const std::string& value) {
        WriteUInt16(static_cast<uint16_t>(value.size()));
        buffer_.insert(buffer_.end(), value.begin(), value.end());
    }

    void BinaryWriter::WriteBytes(const uint8_t* data, size_t length) {
        WriteUInt32(static_cast<uint32_t>(length));
        buffer_.insert(buffer_.end(), data, data + length);
    }

    void BinaryWriter::WriteVector3(const glm::vec3& vec) {
        WriteFloat(vec.x);
        WriteFloat(vec.y);
        WriteFloat(vec.z);
    }

    void BinaryWriter::WriteQuaternion(const glm::quat& quaternion) {
        WriteFloat(quaternion.x);
        WriteFloat(quaternion.y);
        WriteFloat(quaternion.z);
        WriteFloat(quaternion.w);
    }

    void BinaryWriter::WriteJson(const nlohmann::json& json) {
        std::string json_str = json.dump();
        WriteString(json_str);
    }

    const std::vector<uint8_t>& BinaryWriter::GetBuffer() const { return buffer_; }
    size_t BinaryWriter::GetSize() const { return buffer_.size(); }

    void BinaryWriter::Clear() { buffer_.clear(); }

    BinaryReader::BinaryReader(const uint8_t* data, size_t length)
        : data_(data), length_(length) {}

    void BinaryReader::CheckBounds(size_t size) const {
        if (position_ + size > length_) {
            throw std::runtime_error("Read beyond buffer bounds");
        }
    }

    uint8_t BinaryReader::ReadUInt8() {
        CheckBounds(1);
        return data_[position_++];
    }

    uint16_t BinaryReader::ReadUInt16() {
        CheckBounds(2);
        uint16_t value = (static_cast<uint16_t>(data_[position_]) << 8) |
                         static_cast<uint16_t>(data_[position_ + 1]);
        position_ += 2;
        return value;
    }

    uint32_t BinaryReader::ReadUInt32() {
        CheckBounds(4);
        uint32_t value = (static_cast<uint32_t>(data_[position_]) << 24) |
                         (static_cast<uint32_t>(data_[position_ + 1]) << 16) |
                         (static_cast<uint32_t>(data_[position_ + 2]) << 8) |
                         static_cast<uint32_t>(data_[position_ + 3]);
        position_ += 4;
        return value;
    }

    uint64_t BinaryReader::ReadUInt64() {
        CheckBounds(8);
        uint64_t value = 0;
        for (int i = 0; i < 8; ++i) {
            value = (value << 8) | static_cast<uint64_t>(data_[position_ + i]);
        }
        position_ += 8;
        return value;
    }

    int32_t BinaryReader::ReadInt32() {
        return static_cast<int32_t>(ReadUInt32());
    }

    int64_t BinaryReader::ReadInt64() {
        return static_cast<int64_t>(ReadUInt64());
    }

    float BinaryReader::ReadFloat() {
        uint32_t int_value = ReadUInt32();
        float value;
        memcpy(&value, &int_value, sizeof(float));
        return value;
    }

    double BinaryReader::ReadDouble() {
        uint64_t int_value = ReadUInt64();
        double value;
        memcpy(&value, &int_value, sizeof(double));
        return value;
    }

    std::string BinaryReader::ReadString() {
        uint16_t length = ReadUInt16();
        CheckBounds(length);
        std::string str(reinterpret_cast<const char*>(data_ + position_), length);
        position_ += length;
        return str;
    }

    std::vector<uint8_t> BinaryReader::ReadBytes(size_t length) {
        CheckBounds(length);
        std::vector<uint8_t> bytes(data_ + position_, data_ + position_ + length);
        position_ += length;
        return bytes;
    }

    glm::vec3 BinaryReader::ReadVector3() {
        return glm::vec3(ReadFloat(), ReadFloat(), ReadFloat());
    }

    glm::quat BinaryReader::ReadQuaternion() {
        return glm::quat(ReadFloat(), ReadFloat(), ReadFloat(), ReadFloat());
    }

    nlohmann::json BinaryReader::ReadJson() {
        std::string json_str = ReadString();
        return nlohmann::json::parse(json_str);
    }

    size_t BinaryReader::Remaining() const { return length_ - position_; }
    bool BinaryReader::CanRead(size_t size) const { return position_ + size <= length_; }
    size_t BinaryReader::GetPosition() const { return position_; }

    uint32_t CalculateCRC32(const void* data, size_t length) {
        uint32_t crc = 0xFFFFFFFF;
        const uint8_t* bytes = static_cast<const uint8_t*>(data);
        for (size_t i = 0; i < length; ++i) {
            crc ^= bytes[i];
            for (int j = 0; j < 8; ++j) {
                uint32_t mask = -(crc & 1);
                crc = (crc >> 1) ^ (0xEDB88320 & mask);
            }
        }
        return ~crc;
    }

    std::vector<uint8_t> CompressData(const std::vector<uint8_t>& data, int level) {
        if (data.empty()) return {};
        uLongf compressed_size = compressBound(data.size());
        std::vector<uint8_t> compressed(compressed_size);
        if (compress2(compressed.data(), &compressed_size,
                     data.data(), data.size(), level) != Z_OK) {
            throw std::runtime_error("Compression failed");
        }
        compressed.resize(compressed_size);
        return compressed;
    }

    std::vector<uint8_t> DecompressData(const std::vector<uint8_t>& compressed) {
        if (compressed.empty()) return {};
        if (compressed.size() < 4) {
            throw std::runtime_error("Invalid compressed data");
        }
        uint32_t original_size = *reinterpret_cast<const uint32_t*>(compressed.data());
        std::vector<uint8_t> decompressed(original_size);
        uLongf decompressed_size = original_size;
        if (uncompress(decompressed.data(), &decompressed_size,
                      compressed.data() + 4, compressed.size() - 4) != Z_OK) {
            throw std::runtime_error("Decompression failed");
        }
        return decompressed;
    }

    std::vector<uint8_t> ProtocolCapabilities::Serialize() const {
        BinaryWriter writer;
        writer.WriteUInt8(version);
        writer.WriteUInt8(supports_compression ? 1 : 0);
        writer.WriteUInt8(supports_encryption ? 1 : 0);
        writer.WriteUInt32(max_message_size);
        writer.WriteUInt16(static_cast<uint16_t>(supported_message_types.size()));
        for (uint16_t type : supported_message_types) {
            writer.WriteUInt16(type);
        }
        return writer.GetBuffer();
    }

    ProtocolCapabilities ProtocolCapabilities::Deserialize(const uint8_t* data, size_t length) {
        BinaryReader reader(data, length);
        ProtocolCapabilities caps;
        caps.version = reader.ReadUInt8();
        caps.supports_compression = reader.ReadUInt8() != 0;
        caps.supports_encryption = reader.ReadUInt8() != 0;
        caps.max_message_size = reader.ReadUInt32();
        uint16_t type_count = reader.ReadUInt16();
        caps.supported_message_types.reserve(type_count);
        for (uint16_t i = 0; i < type_count; ++i) {
            caps.supported_message_types.push_back(reader.ReadUInt16());
        }
        return caps;
    }

} // namespace BinaryProtocol
