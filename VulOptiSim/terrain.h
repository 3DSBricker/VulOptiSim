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
    
    // Zorg dat de definitie HIER in de header staat:
    inline float get_height_fast(const glm::vec2& pos) const 
    {
        // Terug naar je originele deling (die werkt!), maar we gebruiken [] in plaats van .at()
        // Dit verwijdert de 'bounds-check' overhead, wat de winst oplevert.
        int x = static_cast<int>(pos.x / tile_width);
        int y = static_cast<int>(pos.y / tile_length);

        // [] is de snelle variant zonder bounds-check
        // We casten hier niet naar float omdat terrain_heights een vector van floats is.
        return terrain_heights[get_tile_index(x, y)];
    }
    
    float get_height(const glm::vec2& position2d) const;

    std::vector<glm::vec2> find_route(const glm::vec2& start_position, const glm::vec2& target_position) const;

    bool in_bounds(const glm::vec2& position2d) const;
    void clamp_to_bounds(glm::vec2& position2d) const;

    size_t get_voxel_count() const { return terrain_transforms.size(); }

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
    std::vector<glm::mat4> terrain_transforms;
    std::vector<uint32_t> texture_indices;
    
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
    
    std::vector<Greedy_Tile> generate_greedy_mesh(
    const std::vector<Tile_Data>& map_data,int lowest);


private:

    std::vector<glm::vec2> reconstruct_path(const std::unordered_map<glm::ivec2, glm::ivec2>& parents, const glm::ivec2& start_position, const glm::ivec2& target_position) const;
    std::vector<glm::ivec2> get_neighbours(const glm::ivec2& node) const;
    bool is_accessible(const glm::ivec2& tile, const glm::ivec2& from) const;
    bool is_initialized = false;
    
    std::vector<glm::vec4> terrain_uvs;

    // Sla hier de unieke GPU handle op
    uint32_t terrain_gpu_handle = 0;
    
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
    
    
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
    };



    std::vector<Tile_Data> read_map_file(const std::filesystem::path& path_to_height_map, int& map_width, int& map_length) const;
    
    int get_tile_index(const int x, const int y) const;
};
