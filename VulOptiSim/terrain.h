#pragma once

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

    void draw(vulvox::Renderer* renderer) const;

    float get_height(const glm::vec2& position2d) const;

    std::vector<glm::vec2> find_route(const glm::vec2& start_position, const glm::vec2& target_position) const;

    bool in_bounds(const glm::vec2& position2d) const;
    void clamp_to_bounds(glm::vec2& position2d) const;

    int map_width = 0;
    int map_length = 0;

    float tile_width = 6.f;
    float tile_length = 6.f;
    float tile_height = 6.f;

    float terrain_width = 0.f;
    float terrain_length = 0.f;

    std::vector<Terrain_Types> tile_types;
    std::vector<float> terrain_heights;
    std::vector<glm::mat4> terrain_transforms;
    std::vector<uint32_t> texture_indices;


private:

    std::vector<glm::vec2> reconstruct_path(const std::unordered_map<glm::ivec2, glm::ivec2>& parents, const glm::ivec2& start_position, const glm::ivec2& target_position) const;
    std::vector<glm::ivec2> get_neighbours(const glm::ivec2& node) const;
    bool is_accessible(const glm::ivec2& tile, const glm::ivec2& from) const;

    // Struct voor A* knopen (met g-cost, h-cost en f-cost)
    struct Node {
        glm::ivec2 position;
        float g_cost = 0.0f;  // Werkelijke kosten van start tot deze knoop
        float h_cost = 0.0f;  // Geschatte kosten van deze knoop naar het doel
        float f_cost = 0.0f;  // Totaal van g_cost + h_cost

        bool operator>(const Node& other) const {
            return f_cost > other.f_cost;  // Prioriteitsqueue sorteert op f_cost
        }
    };

    struct Tile_Data
    {
        uint32_t height;
        uint32_t tile_type;
        uint32_t alpha;
    };

    std::vector<Tile_Data> read_map_file(const std::filesystem::path& path_to_height_map, int& map_width, int& map_length) const;

    int get_tile_index(const int x, const int y) const;
};
