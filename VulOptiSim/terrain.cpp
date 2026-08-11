#include "pch.h"
#include "terrain.h"

// Functie om Manhattan afstand te berekenen (heuristiek) (|dx| + |dy|)
auto heuristic = [](const glm::ivec2& a, const glm::ivec2& b) -> float {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y);
};

namespace glm {
    inline bool operator==(const glm::ivec2& a, const glm::ivec2& b) {
        return a.x == b.x && a.y == b.y;
    }
}

Terrain::Terrain(const std::filesystem::path& path_to_height_map)
{
    map_data = read_map_file(path_to_height_map, map_width, map_length);

    if (map_data.empty()) return;

    terrain_width = static_cast<float>(map_width) * tile_width;
    terrain_length = static_cast<float>(map_length) * tile_length;

    int lowest = std::numeric_limits<int>::max();
    for (const auto& tile : map_data)
    {
        if (tile.height < lowest)
        {
            lowest = std::min(lowest, static_cast<int>(tile.height));
        }
    }
    
    terrain_heights.reserve(map_length * map_width);
    tile_types.reserve(map_length * map_width);

    // Gameplay heightmap bewaren
    for (int z = 0; z < map_length; z++)
    {
        for (int x = 0; x < map_width; x++)
        {
            const auto& tile = map_data[z * map_width + x];
            int height = tile.height - lowest + 1;

            terrain_heights.emplace_back(height * tile_height);
            
            if (tile.tile_type & 1) tile_types.push_back(Terrain_Types::Sea);
            else if (tile.tile_type & 2) tile_types.push_back(Terrain_Types::Grass);
            else if (tile.tile_type & 4) tile_types.push_back(Terrain_Types::Mountain);
            else tile_types.push_back(Terrain_Types::Stone);
        }
    }
}

void Terrain::initialize(vulvox::Renderer* renderer)
{
    std::cout << "TERRAIN INITIALIZE CHUNKS (Culling + Side-Face Generation)\n";
    is_initialized = true;

    int chunks_x = (map_width + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int chunks_z = (map_length + CHUNK_SIZE - 1) / CHUNK_SIZE;
    
    int lowest = std::numeric_limits<int>::max();
    for (const auto& tile : map_data) {
        if (tile.height < lowest) lowest = static_cast<int>(tile.height);
    }

    enum FaceType {
        FACE_TOP   = 0,
        FACE_NORTH = 1, // +Z
        FACE_SOUTH = 2, // -Z
        FACE_EAST  = 3, // +X
        FACE_WEST  = 4  // -X
    };

    // Helper om hoogte van een buur op te vragen (buiten de map = hoogte 0)
    auto get_neighbor_height = [&](int nx, int nz) -> int {
        if (nx >= 0 && nx < map_width && nz >= 0 && nz < map_length) {
            return map_data[get_tile_index(nx, nz)].height - lowest + 1;
        }
        return 0; 
    };

    const int wall_texture_idx = 2; // Textuur index voor muren (bijv. steen/beton)

    for (int cz = 0; cz < chunks_z; cz++) {
        for (int cx = 0; cx < chunks_x; cx++) {
            
            int start_x = cx * CHUNK_SIZE;
            int start_z = cz * CHUNK_SIZE;
            int end_x = std::min(start_x + CHUNK_SIZE, map_width);
            int end_z = std::min(start_z + CHUNK_SIZE, map_length);

            std::vector<vulvox::Vulkan_Engine::TerrainInstanceData> chunk_instances;
            chunk_instances.reserve(CHUNK_SIZE * CHUNK_SIZE * 5); // Max 1 top + 4 muren

            for (int z = start_z; z < end_z; z++) {
                for (int x = start_x; x < end_x; x++) {
                    
                    int global_idx = get_tile_index(x, z);
                    const auto& tile = map_data[global_idx];
                    int h_curr = tile.height - lowest + 1;
                    
                    int tex = 3; 
                    if (tile.tile_type & 1) tex = 0;
                    else if (tile.tile_type & 2) tex = 1;
                    else if (tile.tile_type & 4) tex = 2;

                    // -------------------------------------------------------------
                    // 1. TOP FACE (Horizontaal dak)
                    // -------------------------------------------------------------
                    vulvox::Vulkan_Engine::TerrainInstanceData top;
                    top.position_tex = glm::vec4(
                        x * tile_width + tile_width * 0.5f,
                        h_curr * tile_height, 
                        z * tile_length + tile_length * 0.5f,
                        static_cast<float>(tex)
                    );
                    top.scale_pad = glm::vec4(tile_width, tile_height, tile_length, FACE_TOP);
                    chunk_instances.push_back(top);

                    // -------------------------------------------------------------
                    // 2. SIDE FACES (Verticale Muren)
                    // -------------------------------------------------------------
                    
                    // OOST ( +X )
                    int h_east = get_neighbor_height(x + 1, z);
                    if (h_curr > h_east) {
                        float wall_h = (h_curr - h_east) * tile_height;
                        float center_y = (h_east + h_curr) * 0.5f * tile_height;
                        float center_x = (x + 1) * tile_width; // Precies op de rand tussen x en x+1
                        float center_z = (z + 0.5f) * tile_length;

                        vulvox::Vulkan_Engine::TerrainInstanceData wall;
                        wall.position_tex = glm::vec4(center_x, center_y, center_z, static_cast<float>(wall_texture_idx));
                        wall.scale_pad = glm::vec4(tile_width, wall_h, tile_length, FACE_EAST);
                        chunk_instances.push_back(wall);
                    }

                    // WEST ( -X )
                    int h_west = get_neighbor_height(x - 1, z);
                    if (h_curr > h_west) {
                        float wall_h = (h_curr - h_west) * tile_height;
                        float center_y = (h_west + h_curr) * 0.5f * tile_height;
                        float center_x = x * tile_width; // Precies op de rand van tegel x
                        float center_z = (z + 0.5f) * tile_length;

                        vulvox::Vulkan_Engine::TerrainInstanceData wall;
                        wall.position_tex = glm::vec4(center_x, center_y, center_z, static_cast<float>(wall_texture_idx));
                        wall.scale_pad = glm::vec4(tile_width, wall_h, tile_length, FACE_WEST);
                        chunk_instances.push_back(wall);
                    }

                    // NOORD ( +Z )
                    int h_north = get_neighbor_height(x, z + 1);
                    if (h_curr > h_north) {
                        float wall_h = (h_curr - h_north) * tile_height;
                        float center_y = (h_north + h_curr) * 0.5f * tile_height;
                        float center_x = (x + 0.5f) * tile_width;
                        float center_z = (z + 1) * tile_length; // Precies op de rand tussen z en z+1

                        vulvox::Vulkan_Engine::TerrainInstanceData wall;
                        wall.position_tex = glm::vec4(center_x, center_y, center_z, static_cast<float>(wall_texture_idx));
                        wall.scale_pad = glm::vec4(tile_width, wall_h, tile_length, FACE_NORTH);
                        chunk_instances.push_back(wall);
                    }

                    // ZUID ( -Z )
                    int h_south = get_neighbor_height(x, z - 1);
                    if (h_curr > h_south) {
                        float wall_h = (h_curr - h_south) * tile_height;
                        float center_y = (h_south + h_curr) * 0.5f * tile_height;
                        float center_x = (x + 0.5f) * tile_width;
                        float center_z = z * tile_length; // Precies op de rand van tegel z

                        vulvox::Vulkan_Engine::TerrainInstanceData wall;
                        wall.position_tex = glm::vec4(center_x, center_y, center_z, static_cast<float>(wall_texture_idx));
                        wall.scale_pad = glm::vec4(tile_width, wall_h, tile_length, FACE_SOUTH);
                        chunk_instances.push_back(wall);
                    }
                }
            }

            if (chunk_instances.empty()) continue;

            TerrainChunk chunk;
            chunk.gpu_handle = renderer->register_static_instances(chunk_instances);
            
            float mid_x = (start_x + (end_x - start_x) * 0.5f) * tile_width;
            float mid_z = (start_z + (end_z - start_z) * 0.5f) * tile_length;
            chunk.center = glm::vec2(mid_x, mid_z);
            chunk.radius = (CHUNK_SIZE * tile_width) * 0.75f; 
            chunk.is_empty = false;
            
            chunks.push_back(chunk);
        }
    }
}

void Terrain::draw(vulvox::Renderer* renderer, const glm::vec3& camera_position) const
{
    float render_distance = 2500.0f; 
    glm::vec2 cam_pos_2d(camera_position.x, camera_position.z);

    for (const auto& chunk : chunks) {
        if (chunk.is_empty) continue;

        // Snelle culling zonder glm::distance / sqrt
        float dx = cam_pos_2d.x - chunk.center.x;
        float dy = cam_pos_2d.y - chunk.center.y;
        float dist_sq = (dx * dx) + (dy * dy);
        
        float cull_dist = render_distance + chunk.radius;

        if (dist_sq < (cull_dist * cull_dist)) {
            renderer->draw_static_instanced("texture_array_test", chunk.gpu_handle);
        }
    }
}

std::vector<Terrain::Greedy_Tile> Terrain::generate_greedy_mesh_for_chunk(
    const std::vector<Tile_Data>& map_data, int lowest, 
    int start_x, int end_x, 
    int start_z, int end_z)
{
    std::vector<Greedy_Tile> result;
    
    int chunk_width = end_x - start_x;
    int chunk_length = end_z - start_z;
    std::vector<bool> visited(chunk_width * chunk_length, false);

    for (int z = start_z; z < end_z; z++)
    {
        for (int x = start_x; x < end_x; x++)
        {
            int local_index = (z - start_z) * chunk_width + (x - start_x);
            int global_index = z * map_width + x;

            if (visited[local_index]) continue;

            const auto& tile = map_data[global_index];

            int texture;
            if(tile.tile_type & 1) texture = 0;
            else if(tile.tile_type & 2) texture = 1;
            else if(tile.tile_type & 4) texture = 2;
            else texture = 3;

            int height = tile.height - lowest + 1;

            int width = 1;
            while(x + width < end_x)
            {
                int next_local = (z - start_z) * chunk_width + (x + width - start_x);
                int next_global = z * map_width + (x + width);

                if(visited[next_local]) break;

                auto& t = map_data[next_global];
                
                int tex;
                if(t.tile_type & 1) tex = 0;
                else if(t.tile_type & 2) tex = 1;
                else if(t.tile_type & 4) tex = 2;
                else tex = 3;

                if(t.height - lowest + 1 != height || tex != texture) break;
                width++;
            }

            int length = 1;
            bool stop = false;
            while(z + length < end_z && !stop)
            {
                for(int xx = 0; xx < width; xx++)
                {
                    int next_local = (z + length - start_z) * chunk_width + (x + xx - start_x);
                    int next_global = (z + length) * map_width + (x + xx);

                    if(visited[next_local]) { stop = true; break; }

                    auto& t = map_data[next_global];

                    int tex;
                    if(t.tile_type & 1) tex = 0;
                    else if(t.tile_type & 2) tex = 1;
                    else if(t.tile_type & 4) tex = 2;
                    else tex = 3;

                    if(t.height - lowest + 1 != height || tex != texture) { stop = true; break; }
                }
                if(!stop) length++;
            }

            for(int zz = 0; zz < length; zz++)
            {
                for(int xx = 0; xx < width; xx++)
                {
                    visited[(z + zz - start_z) * chunk_width + (x + xx - start_x)] = true;
                }
            }

            result.push_back({x, z, width, length, height, texture});
        }
    }
    return result;
}

float Terrain::get_height(const glm::vec2& position2d) const
{
    if (!in_bounds(position2d)) return 0.f;

    int x = static_cast<int>(position2d.x * inv_tile_width);
    int y = static_cast<int>(position2d.y * inv_tile_length);

    return terrain_heights[get_tile_index(x, y)];
}

std::vector<glm::vec2> Terrain::find_route(const glm::vec2& start_position, const glm::vec2& target_position) const
{
    glm::ivec2 start_tile{ start_position.x * inv_tile_width, start_position.y * inv_tile_length };
    glm::ivec2 target_tile{ target_position.x * inv_tile_width, target_position.y * inv_tile_length };

    struct SearchNode {
        glm::ivec2 pos; 
        float g_cost; 
        float f_cost; 
        bool operator<(const SearchNode& other) const {
            return f_cost > other.f_cost; 
        }
    };

    std::priority_queue<SearchNode> open_set;
    open_set.push({ start_tile, 0.f, heuristic(start_tile, target_tile) });

    // FIX: Gebruik map_width * map_length (geen enorme terrain_width allocatie meer!)
    std::vector<bool> visited(map_width * map_length, false);
    std::unordered_map<glm::ivec2, glm::ivec2, IVec2Hash> parents;

    while (!open_set.empty())
    {
        SearchNode current = open_set.top(); 
        open_set.pop();

        if (current.pos == target_tile) {
            return reconstruct_path(parents, start_tile, current.pos);
        }

        int index = get_tile_index(current.pos.x, current.pos.y);
        if (visited[index]) continue;
        visited[index] = true;

        auto neighbours = get_neighbours(current.pos);

        for (const glm::ivec2& neighbour : neighbours)
        {
            int neighbour_index = get_tile_index(neighbour.x, neighbour.y);
            if (!visited[neighbour_index]) {
                parents[neighbour] = current.pos;
                
                float g_cost = current.g_cost + 1.0f; 
                float f_cost = g_cost + heuristic(neighbour, target_tile);
                
                open_set.push({ neighbour, g_cost, f_cost }); 
            }
        }
    }

    return std::vector<glm::vec2>();
}

bool Terrain::in_bounds(const glm::vec2& position2d) const
{
    return (position2d.x > 0.f && position2d.y > 0.f && position2d.x < terrain_width && position2d.y < terrain_length);
}

void Terrain::clamp_to_bounds(glm::vec2& position2d) const
{
    position2d.x = std::clamp(position2d.x, 0.f, terrain_width);
    position2d.y = std::clamp(position2d.y, 0.f, terrain_length);
}

std::vector<glm::vec2> Terrain::reconstruct_path(const std::unordered_map<glm::ivec2, glm::ivec2, IVec2Hash>& parents, const glm::ivec2& start_position, const glm::ivec2& target_position) const
{
    std::vector<glm::vec2> path;
    for (glm::ivec2 current = target_position; current != start_position; current = parents.at(current))
    {
        path.emplace_back(static_cast<float>(current.x) * tile_width + tile_width / 2.f, static_cast<float>(current.y) * tile_length + tile_length / 2.f);
    }
    return path;
}

std::vector<glm::ivec2> Terrain::get_neighbours(const glm::ivec2& node) const
{
    std::vector<glm::ivec2> neighbours;

    if (node.x > 0 && is_accessible({ node.x - 1, node.y }, node))
        neighbours.push_back({ node.x - 1, node.y });
    if (node.y > 0 && is_accessible({ node.x, node.y - 1 }, node))
        neighbours.push_back({ node.x, node.y - 1 });
    if (node.x < map_width - 1 && is_accessible({ node.x + 1, node.y }, node))
        neighbours.push_back({ node.x + 1, node.y });
    if (node.y < map_length - 1 && is_accessible({ node.x, node.y + 1 }, node))
        neighbours.push_back({ node.x, node.y + 1 });

    return neighbours;
}

bool Terrain::is_accessible(const glm::ivec2& tile, const glm::ivec2& from) const
{
    int tile_index = get_tile_index(tile.x, tile.y);
    int from_index = get_tile_index(from.x, from.y);

    if (tile_types[tile_index] == Terrain_Types::Sea || tile_types[tile_index] == Terrain_Types::Mountain) return false;
    if (terrain_heights[tile_index] - terrain_heights[from_index] > 3) return false;

    return true;
}

std::vector<Terrain::Tile_Data> Terrain::read_map_file(const std::filesystem::path& path_to_height_map, int& map_width, int& map_length) const
{
    const int channels_used = 4;
    int channels;
    int image_width;
    int image_height;

    unsigned char* image_data = stbi_load(path_to_height_map.string().c_str(), &image_width, &image_height, &channels, channels_used);

    map_width = image_width;
    map_length = image_height;

    std::vector<Tile_Data> map_tiles;
    map_tiles.reserve(image_width * image_height);

    for (size_t y = 0; y < image_height; y++)
    {
        for (size_t x = 0; x < image_width; x++)
        {
            int pixel_index = (y * image_width + x) * 4;

            uint32_t h1 = image_data[pixel_index + 0];
            uint32_t h2 = image_data[pixel_index + 1];
            uint32_t tile_type = image_data[pixel_index + 2];
            uint32_t alpha = image_data[pixel_index + 3];

            Tile_Data tile{};
            tile.height = (h1 << 8) | h2;
            tile.tile_type = tile_type;
            tile.alpha = alpha;

            map_tiles.push_back(tile);
        }
    }

    stbi_image_free(image_data);
    return map_tiles;
}