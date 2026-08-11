#pragma once

#include <vector>
#include <filesystem>
#include <unordered_map>
#include <queue>
#include <glm/glm.hpp>
#include "renderer.h"

// Hash functie voor glm::ivec2 zodat unordered_set en unordered_map sneller werken
struct IVec2Hash {
    std::size_t operator()(const glm::ivec2& v) const noexcept {
        return std::hash<int>()(v.x) ^ (std::hash<int>()(v.y) << 1);
    }
};

class Terrain
{
public:
    enum class Terrain_Types
    {
        Sea,
        Grass,
        Mountain,
        Stone
    };

    Terrain() = default;
    Terrain(const std::filesystem::path& path_to_height_map);

    void initialize(vulvox::Renderer* renderer);
    void draw(vulvox::Renderer* renderer, const glm::vec3& camera_position) const;
    
    // EXTREEM SNEL: Geen bounds check, gebruikt vermenigvuldiging ipv deling
    inline float get_height_fast(const glm::vec2& pos) const 
    {
        int x = static_cast<int>(pos.x * inv_tile_width);
        int y = static_cast<int>(pos.y * inv_tile_length);
        return terrain_heights[get_tile_index(x, y)];
    }
    
    float get_height(const glm::vec2& position2d) const;
    std::vector<glm::vec2> find_route(const glm::vec2& start_position, const glm::vec2& target_position) const;

    bool in_bounds(const glm::vec2& position2d) const;
    void clamp_to_bounds(glm::vec2& position2d) const;

    int map_width = 0;
    int map_length = 0;

    float tile_width = 6.f;
    float tile_length = 6.f;
    float tile_height = 6.f;
    
    float inv_tile_width = 1.0f / tile_width;
    float inv_tile_length = 1.0f / tile_length;
    
    float terrain_width = 0.f;
    float terrain_length = 0.f;

    std::vector<Terrain_Types> tile_types;
    std::vector<float> terrain_heights;
    
    struct Greedy_Tile
    {
        int x;
        int z;
        int width;
        int length;
        int height;
        int texture;
    };
    
    struct Tile_Data
    {
        uint32_t height;
        uint32_t tile_type;
        uint32_t alpha;
    };

    std::vector<Greedy_Tile> generate_greedy_mesh_for_chunk(
        const std::vector<Tile_Data>& map_data, 
        int lowest, 
        int start_x, int end_x, 
        int start_z, int end_z);
    
    std::vector<Tile_Data> map_data;

private:
    std::vector<glm::vec2> reconstruct_path(const std::unordered_map<glm::ivec2, glm::ivec2, IVec2Hash>& parents, const glm::ivec2& start_position, const glm::ivec2& target_position) const;
    std::vector<glm::ivec2> get_neighbours(const glm::ivec2& node) const;
    bool is_accessible(const glm::ivec2& tile, const glm::ivec2& from) const;
    
    bool is_initialized = false;
    
    struct TerrainChunk {
        vulvox::Vulkan_Engine::StaticInstanceHandle gpu_handle;
        glm::vec2 center;       // Voor snelle afstand- of culling checks
        float radius;           // Bounding sphere radius van deze chunk
        bool is_empty = false;
    };

    std::vector<TerrainChunk> chunks;
    const int CHUNK_SIZE = 64; // 64x64 tiles per chunk
    
    struct Node {
        glm::ivec2 position;
        float g_cost = 0.0f;
        float h_cost = 0.0f;
        float f_cost = 0.0f;

        bool operator>(const Node& other) const {
            return f_cost > other.f_cost;
        }
    };

    std::vector<Tile_Data> read_map_file(const std::filesystem::path& path_to_height_map, int& map_width, int& map_length) const;
    
    inline int get_tile_index(const int x, const int y) const {
        return (y * map_width) + x;
    }
};