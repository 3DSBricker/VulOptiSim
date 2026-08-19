#include "pch.h"
#include "terrain.h"
#include "geo_utils.h"
#include "math_utils.h"
#include "algo_utils.h"
#include <iostream>

// Custom Platte Min-Heap voor A* (Vervangt std::priority_queue)
struct AStarNode {
    int tile_idx;
    glm::ivec2 pos;
    float g_cost;
    float f_cost;
};

class FlatMinHeap {
private:
    std::vector<AStarNode> heap;
public:
    FlatMinHeap() { heap.reserve(1024); }

    void push(const AStarNode& node) {
        heap.push_back(node);
        size_t idx = heap.size() - 1;
        while (idx > 0) {
            size_t parent = (idx - 1) / 2;
            if (heap[idx].f_cost < heap[parent].f_cost) {
                AStarNode temp = heap[idx];
                heap[idx] = heap[parent];
                heap[parent] = temp;
                idx = parent;
            } else break;
        }
    }

    AStarNode pop() {
        AStarNode top = heap[0];
        heap[0] = heap.back();
        heap.pop_back();
        if (!heap.empty()) {
            size_t idx = 0;
            size_t size = heap.size();
            while (true) {
                size_t left = 2 * idx + 1;
                size_t right = 2 * idx + 2;
                size_t smallest = idx;

                if (left < size && heap[left].f_cost < heap[smallest].f_cost) smallest = left;
                if (right < size && heap[right].f_cost < heap[smallest].f_cost) smallest = right;

                if (smallest != idx) {
                    AStarNode temp = heap[idx];
                    heap[idx] = heap[smallest];
                    heap[smallest] = temp;
                    idx = smallest;
                } else break;
            }
        }
        return top;
    }

    bool empty() const { return heap.empty(); }
};

Terrain::Terrain(const std::filesystem::path& path_to_height_map)
{
    map_data = read_map_file(path_to_height_map, map_width, map_length);
    if (map_data.empty()) return;

    terrain_width = static_cast<float>(map_width) * tile_width;
    terrain_length = static_cast<float>(map_length) * tile_length;

    int lowest = math_utils::INT_MAX_VAL;
    for (const auto& tile : map_data) {
        if (static_cast<int>(tile.height) < lowest) lowest = static_cast<int>(tile.height);
    }

    terrain_heights.reserve(map_length * map_width);
    tile_types.reserve(map_length * map_width);

    for (int z = 0; z < map_length; z++) {
        for (int x = 0; x < map_width; x++) {
            const auto& tile = map_data[z * map_width + x];
            int height = static_cast<int>(tile.height) - lowest + 1;

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
    std::cout << "TERRAIN INITIALIZE CHUNKS (Static GPU Path)\n";
    int chunks_x = (map_width + CHUNK_SIZE - 1) / CHUNK_SIZE;
    int chunks_z = (map_length + CHUNK_SIZE - 1) / CHUNK_SIZE;

    int lowest = math_utils::INT_MAX_VAL;
    for (const auto& tile : map_data) {
        if (static_cast<int>(tile.height) < lowest) lowest = static_cast<int>(tile.height);
    }

    auto get_neighbor_height = [&](int nx, int nz) -> int {
        if (nx >= 0 && nx < map_width && nz >= 0 && nz < map_length) {
            return static_cast<int>(map_data[get_tile_index(nx, nz)].height) - lowest + 1;
        }
        return 0;
    };

    for (int cz = 0; cz < chunks_z; cz++) {
        for (int cx = 0; cx < chunks_x; cx++) {
            int start_x = cx * CHUNK_SIZE;
            int start_z = cz * CHUNK_SIZE;
            int end_x = math_utils::min(start_x + CHUNK_SIZE, map_width);
            int end_z = math_utils::min(start_z + CHUNK_SIZE, map_length);

            // Tijdelijke vectors: Bestaan alleen tijdens initialisatie om VRAM te vullen
            std::vector<glm::mat4> temp_transforms;
            std::vector<uint32_t> temp_texture_indices;
            temp_transforms.reserve(CHUNK_SIZE * CHUNK_SIZE * 2);
            temp_texture_indices.reserve(CHUNK_SIZE * CHUNK_SIZE * 2);

            for (int z = start_z; z < end_z; z++) {
                for (int x = start_x; x < end_x; x++) {
                    int global_idx = get_tile_index(x, z);
                    const auto& tile = map_data[global_idx];
                    int h_curr = static_cast<int>(tile.height) - lowest + 1;

                    int tex = 3;
                    if (tile.tile_type & 1) tex = 0;
                    else if (tile.tile_type & 2) tex = 1;
                    else if (tile.tile_type & 4) tex = 2;

                    int h_east  = get_neighbor_height(x + 1, z);
                    int h_west  = get_neighbor_height(x - 1, z);
                    int h_north = get_neighbor_height(x, z + 1);
                    int h_south = get_neighbor_height(x, z - 1);

                    int min_neighbor_h = math_utils::min(math_utils::min(h_east, h_west), math_utils::min(h_north, h_south));
                    int start_y = math_utils::max(0, math_utils::min(min_neighbor_h, h_curr - 1));

                    for (int y = start_y; y < h_curr; y++) {
                        bool is_top = (y == h_curr - 1);
                        bool exposed_east  = (y >= h_east);
                        bool exposed_west  = (y >= h_west);
                        bool exposed_north = (y >= h_north);
                        bool exposed_south = (y >= h_south);

                        if (is_top || exposed_east || exposed_west || exposed_north || exposed_south) {
                            glm::mat4 voxel_transform = glm::mat4(1.0f);
                            voxel_transform = glm::translate(voxel_transform,
                                glm::vec3(x * tile_width + tile_width * 0.5f,
                                          ((float)y + 0.5f) * tile_height,
                                          z * tile_length + tile_length * 0.5f));

                            voxel_transform = glm::scale(voxel_transform, glm::vec3(tile_width, tile_height, tile_length));

                            temp_transforms.push_back(voxel_transform);
                            temp_texture_indices.push_back(tex);
                        }
                    }
                }
            }

            TerrainChunk chunk;
            if (!temp_transforms.empty()) {
                // HIER GEBEURT DE MAGIE: Converteer naar VRAM en behoud alleen de snelle handle
                chunk.gpu_handle = renderer->create_static_instance_group("cube", "texture_array_test", temp_transforms, temp_texture_indices);
                
                float mid_x = (start_x + (end_x - start_x) * 0.5f) * tile_width;
                float mid_z = (start_z + (end_z - start_z) * 0.5f) * tile_length;
                chunk.center = glm::vec2(mid_x, mid_z);
                chunk.radius = (CHUNK_SIZE * tile_width) * 0.75f;
                chunk.is_empty = false;
            } else {
                chunk.is_empty = true;
            }

            chunks.push_back(chunk);
        }
    }
}

void Terrain::draw(vulvox::Renderer* renderer, const glm::vec3& camera_position, const glm::mat4& view_proj) const
{
    float render_distance = 2500.0f;
    glm::vec2 cam_pos_2d(camera_position.x, camera_position.z);

    auto frustumPlanes = geo_utils::extract_frustum_planes(view_proj);

    // Iteratie over chunks: geen dure memcopy operaties meer!
    for (size_t i = 0; i < chunks.size(); i++) {
        if (chunks[i].is_empty) continue;

        float dx = cam_pos_2d.x - chunks[i].center.x;
        float dy = cam_pos_2d.y - chunks[i].center.y;
        float dist_sq = (dx * dx) + (dy * dy);
        float cull_dist = render_distance + chunks[i].radius;

        if (dist_sq < (cull_dist * cull_dist)) {
            glm::vec3 sphere_center(chunks[i].center.x, 0.0f, chunks[i].center.y);
            if (geo_utils::is_sphere_in_frustum(sphere_center, chunks[i].radius + 50.f, frustumPlanes)) {
                
                // Roep direct de Vulkan GPU Handle aan. Extreem snelle CPU->GPU overhead.
                renderer->draw_static_instance_group(chunks[i].gpu_handle);
                
            }
        }
    }
}

// Zero-Allocation / Zero-Hash Table A* Pathfinding Implementation
std::vector<glm::vec2> Terrain::find_route(const glm::vec2& start_position, const glm::vec2& target_position) const
{
    glm::ivec2 start_tile{ static_cast<int>(start_position.x * inv_tile_width), static_cast<int>(start_position.y * inv_tile_length) };
    glm::ivec2 target_tile{ static_cast<int>(target_position.x * inv_tile_width), static_cast<int>(target_position.y * inv_tile_length) };

    int total_tiles = map_width * map_length;
    int start_idx = get_tile_index(start_tile.x, start_tile.y);
    int target_idx = get_tile_index(target_tile.x, target_tile.y);

    if (start_idx < 0 || start_idx >= total_tiles || target_idx < 0 || target_idx >= total_tiles) {
        return {};
    }

    // O(1) Platte arrays ter vervanging van std::unordered_map
    std::vector<int> parent_map(total_tiles, -1);
    std::vector<float> g_costs(total_tiles, math_utils::FLT_MAX_VAL);
    std::vector<uint8_t> visited(total_tiles, 0);

    FlatMinHeap open_set;

    auto calc_h = [](const glm::ivec2& a, const glm::ivec2& b) -> float {
        return static_cast<float>(math_utils::abs(a.x - b.x) + math_utils::abs(a.y - b.y));
    };

    g_costs[start_idx] = 0.0f;
    open_set.push({ start_idx, start_tile, 0.0f, calc_h(start_tile, target_tile) });

    bool found = false;

    while (!open_set.empty()) {
        AStarNode current = open_set.pop();

        if (current.tile_idx == target_idx) {
            found = true;
            break;
        }

        if (visited[current.tile_idx]) continue;
        visited[current.tile_idx] = 1;

        auto neighbours = get_neighbours(current.pos);
        for (const glm::ivec2& neighbour : neighbours) {
            int n_idx = get_tile_index(neighbour.x, neighbour.y);

            if (!visited[n_idx]) {
                float new_g = current.g_cost + 1.0f;
                if (new_g < g_costs[n_idx]) {
                    g_costs[n_idx] = new_g;
                    parent_map[n_idx] = current.tile_idx;
                    float f_cost = new_g + calc_h(neighbour, target_tile);
                    open_set.push({ n_idx, neighbour, new_g, f_cost });
                }
            }
        }
    }

    if (!found) return {};

    // Reconstruct route
    std::vector<glm::vec2> path;
    int curr = target_idx;
    while (curr != start_idx && curr != -1) {
        int cx = curr % map_width;
        int cy = curr / map_width;
        path.emplace_back(
            static_cast<float>(cx) * tile_width + tile_width * 0.5f,
            static_cast<float>(cy) * tile_length + tile_length * 0.5f
        );
        curr = parent_map[curr];
    }

    return path;
}

float Terrain::get_height(const glm::vec2& position2d) const
{
    if (!in_bounds(position2d)) return 0.f;
    int x = static_cast<int>(position2d.x * inv_tile_width);
    int y = static_cast<int>(position2d.y * inv_tile_length);
    return terrain_heights[get_tile_index(x, y)];
}

bool Terrain::in_bounds(const glm::vec2& position2d) const
{
    return (position2d.x > 0.f && position2d.y > 0.f && position2d.x < terrain_width && position2d.y < terrain_length);
}

void Terrain::clamp_to_bounds(glm::vec2& position2d) const
{
    position2d.x = math_utils::clamp(position2d.x, 0.f, terrain_width);
    position2d.y = math_utils::clamp(position2d.y, 0.f, terrain_length);
}

std::vector<glm::ivec2> Terrain::get_neighbours(const glm::ivec2& node) const
{
    std::vector<glm::ivec2> neighbours;
    neighbours.reserve(4);

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
    if (terrain_heights[tile_index] - terrain_heights[from_index] > 3.0f) return false;

    return true;
}

std::vector<Terrain::Tile_Data> Terrain::read_map_file(const std::filesystem::path& path_to_height_map, int& map_width, int& map_length) const
{
    int channels, image_width, image_height;
    unsigned char* image_data = stbi_load(path_to_height_map.string().c_str(), &image_width, &image_height, &channels, 4);

    if (!image_data) return {};

    map_width = image_width;
    map_length = image_height;

    std::vector<Tile_Data> map_tiles;
    map_tiles.reserve(image_width * image_height);

    for (int y = 0; y < image_height; y++) {
        for (int x = 0; x < image_width; x++) {
            int pixel_index = (y * image_width + x) * 4;

            uint32_t h1 = image_data[pixel_index + 0];
            uint32_t h2 = image_data[pixel_index + 1];
            uint32_t tile_type = image_data[pixel_index + 2];
            uint32_t alpha = image_data[pixel_index + 3];

            map_tiles.push_back({ (h1 << 8) | h2, tile_type, alpha });
        }
    }

    stbi_image_free(image_data);
    return map_tiles;
}