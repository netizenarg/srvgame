#include "game/WorldChunk.hpp"

const float WorldChunk::BLOCK_SIZE = 1.0f;
const float WorldChunk::CHUNK_WIDTH = (DEFAULT_SIZE - 1) * DEFAULT_SPACING;

WorldChunk::WorldChunk(int x, int z, ChunkLOD lod)
    : chunkX_(x), chunkZ_(z), lod_(lod), biome_(BiomeType::PLAINS) {

    blocks_.resize(DEFAULT_SIZE * DEFAULT_SIZE * DEFAULT_SIZE, BlockType::AIR);
    heightmap_.resize(DEFAULT_SIZE * DEFAULT_SIZE, 0.0f);
}

const std::vector<Vertex>& WorldChunk::GetVertices() const { return vertices_; }

const std::vector<Triangle>& WorldChunk::GetTriangles() const { return triangles_; }

const std::vector<glm::vec3>& WorldChunk::GetCollisionVertices() const { return collisionVertices_; }

const std::vector<Triangle>& WorldChunk::GetCollisionTriangles() const { return collisionTriangles_; }

int WorldChunk::GetChunkX() const { return chunkX_; }

int WorldChunk::GetChunkZ() const { return chunkZ_; }

ChunkLOD WorldChunk::GetLOD() const { return lod_; }

BiomeType WorldChunk::GetBiome() const { return biome_; }

void WorldChunk::SetBiome(BiomeType biome) { biome_ = biome; }

glm::vec3 WorldChunk::GetWorldPosition() const {
    return glm::vec3(chunkX_ * CHUNK_WIDTH, 0.0f, chunkZ_ * CHUNK_WIDTH);
}

BlockType WorldChunk::GetBlock(int x, int y, int z) const {
    if (x < 0 || x >= DEFAULT_SIZE || y < 0 || y >= DEFAULT_SIZE || z < 0 || z >= DEFAULT_SIZE) {
        return BlockType::AIR;
    }
    int index = x + y * DEFAULT_SIZE + z * DEFAULT_SIZE * DEFAULT_SIZE;
    return blocks_[index];
}

void WorldChunk::SetBlock(int x, int y, int z, BlockType type) {
    if (x < 0 || x >= DEFAULT_SIZE || y < 0 || y >= DEFAULT_SIZE || z < 0 || z >= DEFAULT_SIZE) {
        return;
    }
    int index = x + y * DEFAULT_SIZE + z * DEFAULT_SIZE * DEFAULT_SIZE;
    blocks_[index] = type;
}

float WorldChunk::GetHeightAt(float x, float z) const {
    int localX = static_cast<int>(std::floor(x)) - chunkX_ * DEFAULT_SIZE;
    int localZ = static_cast<int>(std::floor(z)) - chunkZ_ * DEFAULT_SIZE;
    if (localX < 0 || localX >= DEFAULT_SIZE || localZ < 0 || localZ >= DEFAULT_SIZE) {
        return 0.0f;
    }
    return heightmap_[localX + localZ * DEFAULT_SIZE];
}

void WorldChunk::AddEntity(uint64_t entityId) { entities_.insert(entityId); }

void WorldChunk::RemoveEntity(uint64_t entityId) { entities_.erase(entityId); }

const std::unordered_set<uint64_t>& WorldChunk::GetEntities() const { return entities_; }

bool WorldChunk::HasEntities() const { return !entities_.empty(); }

void WorldChunk::GenerateLowPolyGeometry() {
    vertices_.clear();
    triangles_.clear();
    for (int x = 0; x < DEFAULT_SIZE; ++x) {
        for (int z = 0; z < DEFAULT_SIZE; ++z) {
            float height = heightmap_[x + z * DEFAULT_SIZE];
            int blockHeight = static_cast<int>(std::floor(height));
            for (int y = 0; y <= blockHeight && y < DEFAULT_SIZE; ++y) {
                BlockType type = GetBlock(x, y, z);
                if (type != BlockType::AIR) {
                    GenerateBlockVertices(x, y, z, type);
                }
            }
        }
    }
}

void WorldChunk::GenerateBlockVertices(int x, int y, int z, BlockType type) {
    float px = static_cast<float>(x);
    float py = static_cast<float>(y);
    float pz = static_cast<float>(z);
    glm::vec3 color = GetBlockColor(type);
    if (y == DEFAULT_SIZE - 1 || GetBlock(x, y + 1, z) == BlockType::AIR) {
        glm::vec3 p1(px, py + 1, pz);
        glm::vec3 p2(px + 1, py + 1, pz);
        glm::vec3 p3(px + 1, py + 1, pz + 1);
        glm::vec3 p4(px, py + 1, pz + 1);
        AddQuad(p1, p2, p3, p4, glm::vec3(0, 1, 0), color * 1.2f);
    }
    if (y == 0 || GetBlock(x, y - 1, z) == BlockType::AIR) {
        glm::vec3 p1(px, py, pz);
        glm::vec3 p2(px, py, pz + 1);
        glm::vec3 p3(px + 1, py, pz + 1);
        glm::vec3 p4(px + 1, py, pz);
        AddQuad(p1, p2, p3, p4, glm::vec3(0, -1, 0), color * 0.8f);
    }
    if (z == 0 || GetBlock(x, y, z - 1) == BlockType::AIR) {
        glm::vec3 p1(px, py, pz);
        glm::vec3 p2(px + 1, py, pz);
        glm::vec3 p3(px + 1, py + 1, pz);
        glm::vec3 p4(px, py + 1, pz);
        AddQuad(p1, p2, p3, p4, glm::vec3(0, 0, -1), color);
    }
    if (z == DEFAULT_SIZE - 1 || GetBlock(x, y, z + 1) == BlockType::AIR) {
        glm::vec3 p1(px, py, pz + 1);
        glm::vec3 p2(px, py + 1, pz + 1);
        glm::vec3 p3(px + 1, py + 1, pz + 1);
        glm::vec3 p4(px + 1, py, pz + 1);
        AddQuad(p1, p2, p3, p4, glm::vec3(0, 0, 1), color);
    }
    if (x == 0 || GetBlock(x - 1, y, z) == BlockType::AIR) {
        glm::vec3 p1(px, py, pz);
        glm::vec3 p2(px, py + 1, pz);
        glm::vec3 p3(px, py + 1, pz + 1);
        glm::vec3 p4(px, py, pz + 1);
        AddQuad(p1, p2, p3, p4, glm::vec3(-1, 0, 0), color * 0.9f);
    }
    if (x == DEFAULT_SIZE - 1 || GetBlock(x + 1, y, z) == BlockType::AIR) {
        glm::vec3 p1(px + 1, py, pz);
        glm::vec3 p2(px + 1, py, pz + 1);
        glm::vec3 p3(px + 1, py + 1, pz + 1);
        glm::vec3 p4(px + 1, py + 1, pz);
        AddQuad(p1, p2, p3, p4, glm::vec3(1, 0, 0), color * 0.9f);
    }
}

void WorldChunk::AddQuad(const glm::vec3& p1, const glm::vec3& p2,
                        const glm::vec3& p3, const glm::vec3& p4,
                        const glm::vec3& normal, const glm::vec3& color) {
    uint32_t baseIndex = static_cast<uint32_t>(vertices_.size());
    vertices_.emplace_back(p1, normal, color, glm::vec2(0, 0));
    vertices_.emplace_back(p2, normal, color, glm::vec2(1, 0));
    vertices_.emplace_back(p3, normal, color, glm::vec2(1, 1));
    vertices_.emplace_back(p4, normal, color, glm::vec2(0, 1));
    triangles_.emplace_back(baseIndex, baseIndex + 1, baseIndex + 2);
    triangles_.emplace_back(baseIndex, baseIndex + 2, baseIndex + 3);
}

void WorldChunk::AddTriangle(uint32_t v0, uint32_t v1, uint32_t v2) {
    triangles_.emplace_back(v0, v1, v2);
}

glm::vec3 WorldChunk::GetBlockColor(BlockType type) const {
    switch (type) {
        case BlockType::GRASS: return glm::vec3(0.2f, 0.8f, 0.3f);
        case BlockType::DIRT: return glm::vec3(0.6f, 0.4f, 0.2f);
        case BlockType::STONE: return glm::vec3(0.5f, 0.5f, 0.5f);
        case BlockType::WATER: return glm::vec3(0.2f, 0.4f, 0.8f);
        case BlockType::SAND: return glm::vec3(0.9f, 0.8f, 0.5f);
        case BlockType::SNOW: return glm::vec3(0.95f, 0.95f, 0.95f);
        case BlockType::WOOD: return glm::vec3(0.5f, 0.3f, 0.1f);
        case BlockType::LEAVES: return glm::vec3(0.3f, 0.7f, 0.3f);
        default: return glm::vec3(1.0f, 1.0f, 1.0f);
    }
}

glm::vec3 WorldChunk::GetBiomeColor(BiomeType biome, float height) const {
    switch (biome) {
        case BiomeType::FOREST:
            if (height < 0.3f) return glm::vec3(0.2f, 0.6f, 0.2f);
            else return glm::vec3(0.3f, 0.7f, 0.3f);
        case BiomeType::MOUNTAIN:
            if (height < 0.6f) return glm::vec3(0.4f, 0.4f, 0.4f);
            else return glm::vec3(0.8f, 0.8f, 0.8f);
        case BiomeType::DESERT:
            return glm::vec3(0.9f, 0.8f, 0.5f);
        case BiomeType::OCEAN:
            return glm::vec3(0.1f, 0.3f, 0.6f);
        case BiomeType::RIVER:
            return glm::vec3(0.2f, 0.4f, 0.8f);
        case BiomeType::PLAINS:
        default:
            return glm::vec3(0.4f, 0.7f, 0.3f);
    }
}

void WorldChunk::GenerateCollisionMesh() {
    collisionVertices_.clear();
    collisionTriangles_.clear();
    for (int x = 0; x < DEFAULT_SIZE; ++x) {
        for (int z = 0; z < DEFAULT_SIZE; ++z) {
            float height = heightmap_[x + z * DEFAULT_SIZE];
            if (height > 0) {
                glm::vec3 pos(x, height, z);
                collisionVertices_.push_back(pos);
            }
        }
    }
}

bool WorldChunk::IsPositionInside(const glm::vec3& position) const {
    glm::vec3 chunkPos = GetWorldPosition();
    return position.x >= chunkPos.x && position.x < chunkPos.x + CHUNK_WIDTH &&
           position.z >= chunkPos.z && position.z < chunkPos.z + CHUNK_WIDTH;
}

void WorldChunk::SerializeToWriter(BinaryProtocol::BinaryWriter& writer) const
{
    writer.WriteInt32(chunkX_);
    writer.WriteInt32(chunkZ_);
    std::vector<float> vertexFloats;
    vertexFloats.reserve(vertices_.size() * 6);
    for (const auto& v : vertices_)
    {
        vertexFloats.push_back(v.position.x);
        vertexFloats.push_back(v.position.y);
        vertexFloats.push_back(v.position.z);
        vertexFloats.push_back(v.normal.x);
        vertexFloats.push_back(v.normal.y);
        vertexFloats.push_back(v.normal.z);
    }
    uint32_t vertexDataSize = static_cast<uint32_t>(vertexFloats.size() * sizeof(float));
    writer.WriteUInt32(vertexDataSize);
    writer.WriteBytes(reinterpret_cast<const uint8_t*>(vertexFloats.data()), vertexDataSize);
    std::vector<uint32_t> indexInts;
    indexInts.reserve(triangles_.size() * 3);
    for (const auto& tri : triangles_)
    {
        indexInts.push_back(tri.v0);
        indexInts.push_back(tri.v1);
        indexInts.push_back(tri.v2);
    }
    uint32_t indexDataSize = static_cast<uint32_t>(indexInts.size() * sizeof(uint32_t));
    writer.WriteUInt32(indexDataSize);
    writer.WriteBytes(reinterpret_cast<const uint8_t*>(indexInts.data()), indexDataSize);
    writer.WriteUInt32(0);
}

std::vector<uint8_t> WorldChunk::SerializeBinary() const
{
    BinaryProtocol::BinaryWriter writer;
    SerializeToWriter(writer);
    return writer.GetBuffer();
}

nlohmann::json WorldChunk::SerializeJson() const {
    nlohmann::json data;
    data["x"] = chunkX_;
    data["z"] = chunkZ_;
    data["lod"] = static_cast<int>(lod_);
    data["biome"] = static_cast<int>(biome_);
    data["size"] = DEFAULT_SIZE;
    data["spacing"] = DEFAULT_SPACING;
    data["msg"] = "get_chunk";
    nlohmann::json verticesArray = nlohmann::json::array();
    for (const auto& v : vertices_) {
        verticesArray.push_back(v.position.x);
        verticesArray.push_back(v.position.y);
        verticesArray.push_back(v.position.z);
        verticesArray.push_back(v.normal.x);
        verticesArray.push_back(v.normal.y);
        verticesArray.push_back(v.normal.z);
    }
    data["vertices"] = verticesArray;
    nlohmann::json indicesArray = nlohmann::json::array();
    for (const auto& tri : triangles_) {
        indicesArray.push_back(tri.v0);
        indicesArray.push_back(tri.v1);
        indicesArray.push_back(tri.v2);
    }
    data["indices"] = indicesArray;
    data["timestamp"] = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count();
        return data;
}

void WorldChunk::Deserialize(const nlohmann::json& data) {
    chunkX_ = data.value("chunkX", 0);
    chunkZ_ = data.value("chunkZ", 0);
    lod_ = static_cast<ChunkLOD>(data.value("lod", 0));
    biome_ = static_cast<BiomeType>(data.value("biome", 0));
    if (data.contains("heightmap") && data["heightmap"].is_array()) {
        const auto& heightmapData = data["heightmap"];
        heightmap_.resize(heightmapData.size());
        for (size_t i = 0; i < heightmapData.size(); ++i) {
            heightmap_[i] = heightmapData[i].get<float>();
        }
    }
    if (data.contains("blocks") && data["blocks"].is_array()) {
        const auto& blocksData = data["blocks"];
        blocks_.resize(blocksData.size());
        for (size_t i = 0; i < blocksData.size(); ++i) {
            blocks_[i] = static_cast<BlockType>(blocksData[i].get<int>());
        }
    }
    GenerateGeometry();
}

nlohmann::json WorldChunk::SerializeHeightmap() const {
    nlohmann::json data;
    data["chunkX"] = chunkX_;
    data["chunkZ"] = chunkZ_;
    data["lod"] = static_cast<int>(lod_);
    data["biome"] = static_cast<int>(biome_);
    nlohmann::json heightmapArray = nlohmann::json::array();
    for (float height : heightmap_) {
        heightmapArray.push_back(height);
    }
    data["heightmap"] = heightmapArray;
    nlohmann::json blocksArray = nlohmann::json::array();
    for (BlockType block : blocks_) {
        blocksArray.push_back(static_cast<int>(block));
    }
    data["blocks"] = blocksArray;
    return data;
}

void WorldChunk::GenerateGeometry() { GenerateLowPolyGeometry(); }

void WorldChunk::GenerateHighLODGeometry() { GenerateLowPolyGeometry(); }
void WorldChunk::GenerateMediumLODGeometry() { GenerateLowPolyGeometry(); }
void WorldChunk::GenerateLowLODGeometry() { GenerateLowPolyGeometry(); }
void WorldChunk::GenerateBillboardGeometry() { }

glm::vec3 WorldChunk::GetCenter() const {
    return glm::vec3(
        chunkX_ * CHUNK_WIDTH + CHUNK_WIDTH / 2.0f,
        0.0f,
        chunkZ_ * CHUNK_WIDTH + CHUNK_WIDTH / 2.0f
    );
}
