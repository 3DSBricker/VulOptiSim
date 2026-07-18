#include "pch.h"
#include "terrain.h"

Terrain::Terrain(const std::filesystem::path& path_to_height_map)
{
    std::vector<Tile_Data> map_data = read_map_file(path_to_height_map, map_width, map_length);
    terrain_uvs.reserve(map_length * map_width);

    if (map_data.empty())
    {
        return;
    }

    terrain_width = static_cast<float>(map_width) * tile_width;
    terrain_length = static_cast<float>(map_length) * tile_length;

    terrain_transforms.reserve(map_length * map_width);
    texture_indices.reserve(map_length * map_width);
    terrain_heights.reserve(map_length * map_width);
    tile_types.reserve(map_length * map_width);

    int lowest = std::numeric_limits<int>::max();
    for (const auto& tile : map_data)
    {
        if (tile.height < lowest)
        {
            lowest = std::min(lowest, static_cast<int>(tile.height));
        }
    }
    
    // gameplay heightmap bewaren
    for (int z = 0; z < map_length; z++)
    {
        for (int x = 0; x < map_width; x++)
        {
            const auto& tile = map_data[z * map_width + x];

            int height = tile.height - lowest + 1;

            terrain_heights.emplace_back(
                height * tile_height
            );
            
            if (tile.tile_type & 1)
            {
                tile_types.push_back(Terrain_Types::Sea);
            }
            else if (tile.tile_type & 2)
            {
                tile_types.push_back(Terrain_Types::Grass);
            }
            else if (tile.tile_type & 4)
            {
                tile_types.push_back(Terrain_Types::Mountain);
            }
            else
            {
                tile_types.push_back(Terrain_Types::Stone);
            }
        }
    }
    
    auto greedy_tiles = generate_greedy_mesh(map_data, lowest);
    for(const auto& tile : greedy_tiles)
    {
        texture_indices.push_back(tile.texture); // Of tile.texture
        glm::mat4 transform(1.0f);
    
        float w = tile.width * tile_width;
        float l = tile.length * tile_length;
        float h = tile.height * tile_height; // De totale hoogte in 3D-wereld eenheden

        // 1. Verplaats de kubus
        // Als het middelpunt van jouw kubus in het centrum ligt (0,0,0),
        // dan moet de Y-positie op de helft van de totale hoogte zijn, zodat de bodem op 0 raakt.
        transform = glm::translate(
            transform,
            glm::vec3(
                tile.x * tile_width + w * 0.5f,
                h * 0.5f, // Zet de kubus op de helft van zijn eigen hoogte
                tile.z * tile_length + l * 0.5f
            )
        );

        // 2. Schaal de kubus op alle 3 de assen
        transform = glm::scale(
            transform,
            glm::vec3(w, h, l) // Rek hem uit op de Y-as in plaats van 1.0f!
        );
    
        terrain_transforms.push_back(transform);
        
        terrain_uvs.emplace_back(
            0.f,
            0.f,
            static_cast<float>(tile.width),
            static_cast<float>(tile.length)
);
    }
    
}

bool is_initialized = false;  // Bijhouden of terrein al geïnitialiseerd is
void Terrain::initialize(vulvox::Renderer* renderer)
{
    std::cout << "TERRAIN INITIALIZE\n";
    is_initialized = true;
    terrain_gpu_handle = renderer->register_static_instances(terrain_transforms, texture_indices);
}

// void Terrain::draw(vulvox::Renderer* renderer) const
// {
//     renderer->draw_planes(
//         "texture_array_test",
//         terrain_transforms,
//         texture_indices,
//         terrain_uvs);
// }

void Terrain::draw(vulvox::Renderer* renderer) const
{
    renderer->draw_static_instanced("cube", "texture_array_test", terrain_gpu_handle);
}

    std::vector<Terrain::Greedy_Tile> Terrain::generate_greedy_mesh(
    const std::vector<Tile_Data>& map_data, int lowest)
    {
        std::vector<Greedy_Tile> result;

        std::vector<bool> visited(map_width * map_length, false);


        for (int z = 0; z < map_length; z++)
        {
            for (int x = 0; x < map_width; x++)
            {
                int index = z * map_width + x;

                if (visited[index])
                    continue;


                const auto& tile = map_data[index];


                int texture;

                if(tile.tile_type & 1)
                    texture = 0;
                else if(tile.tile_type & 2)
                    texture = 1;
                else if(tile.tile_type & 4)
                    texture = 2;
                else
                    texture = 3;


                int height = tile.height - lowest + 1;


                // zoek maximale breedte
                int width = 1;

                while(x + width < map_width)
                {
                    int i = z * map_width + x + width;

                    if(visited[i])
                        break;


                    auto& t = map_data[i];


                    int tex;

                    if(t.tile_type & 1)
                        tex = 0;
                    else if(t.tile_type & 2)
                        tex = 1;
                    else if(t.tile_type & 4)
                        tex = 2;
                    else
                        tex = 3;


                    if(t.height - lowest + 1 != height || tex != texture)
                        break;


                    width++;
                }


                // zoek lengte
                int length = 1;

                bool stop=false;

                while(z + length < map_length && !stop)
                {
                    for(int xx=0; xx<width; xx++)
                    {
                        int i = (z+length)*map_width + x+xx;


                        if(visited[i])
                        {
                            stop=true;
                            break;
                        }


                        auto& t = map_data[i];


                        int tex;

                        if(t.tile_type & 1)
                            tex=0;
                        else if(t.tile_type &2)
                            tex=1;
                        else if(t.tile_type &4)
                            tex=2;
                        else
                            tex=3;


                        if(t.height - lowest + 1 != height || tex != texture)
                        {
                            stop=true;
                            break;
                        }
                    }


                    if(!stop)
                        length++;
                }



                // markeer gebruikt
                for(int zz=0; zz<length; zz++)
                {
                    for(int xx=0; xx<width; xx++)
                    {
                        visited[(z+zz)*map_width+x+xx]=true;
                    }
                }



                result.push_back(
                {
                    x,
                    z,
                    width,
                    length,
                    height,
                    texture
                });
            }
        }


        return result;
    }

float Terrain::get_height(const glm::vec2& position2d) const
{
    if (!in_bounds(position2d))
    {
        return 0.f;
    }

    int x = static_cast<int>(position2d.x / tile_width);
    int y = static_cast<int>(position2d.y / tile_length);

    int index = get_tile_index(x, y);

    return terrain_heights.at(index);
}

// functie om Manhattan afstand te berekenen (heuristiek) (|dx| + |dy|)
auto heuristic = [](const glm::ivec2& a, const glm::ivec2& b) -> float {
    return std::abs(a.x - b.x) + std::abs(a.y - b.y); // Manhattan afstand
    };


namespace glm {
    inline bool operator==(const glm::ivec2& a, const glm::ivec2& b) {
        return a.x == b.x && a.y == b.y;
    }
}


/// <summary>
/// Uses a pathfinding algorithm to find the shortest path from given start_position to target_position.
/// Note: Path is stored from end to start point.
/// A* implementatie scheelt 10 seconden
/// </summary>
std::vector<glm::vec2> Terrain::find_route(const glm::vec2& start_position, const glm::vec2& target_position) const
{
    // converteer world posities naar grid tile indices
    glm::ivec2 start_tile{ start_position.x / tile_width, start_position.y / tile_length };
    glm::ivec2 target_tile{ target_position.x / tile_width, target_position.y / tile_length };

    // grid dimensies
    int max_width = terrain_width;
    int max_height = terrain_length;

    // node structure voor A* priority queue
    struct Node {
        glm::ivec2 pos; // huidig tile position
        float cost; // est. cost (G + H in A*)

        // comparison operator voor priority queue (min-heap: lager cost nodes eerts verwerken)
        bool operator<(const Node& other) const {
            return cost > other.cost; // min-heap: kleinste kost bovenaan
        }
    };

    // priority queue (open set) voor A* search, slaat nodes op die geevalueerd moeten worden
    std::priority_queue<Node> open_set;
    open_set.push({ start_tile, 0.f }); // start node met zero cost

    // visited nodes (1D vector voor efficiency)
    std::vector<bool> visited(max_width * max_height, false);
    // parent map om pad reconstrueren (slaat op waar elke node vandaan kwam)
    std::unordered_map<glm::ivec2, glm::ivec2> parents;

    // A* main loop
    while (!open_set.empty())
    {
        Node current = open_set.top();  // krijg node met laagste cost
        open_set.pop();

        // als target bereikt, reconstrueer en return pad
        if (current.pos == target_tile)
        {
            return reconstruct_path(parents, start_tile, current.pos);
        }

        // converteer 2D grid positie naar 1D index voor visited array
        int index = current.pos.x * max_width + current.pos.y;
        if (visited[index]) continue;   // skip als al processed
        visited[index] = true;  // markeer visited
        auto neighbours = get_neighbours(current.pos);

        // verwerk alle valid neighboring tiles
        for (const glm::ivec2& neighbour : neighbours)
        {
            int neighbour_index = neighbour.x * max_width + neighbour.y;
            if (!visited[neighbour_index]) {
                parents[neighbour] = current.pos;   // sla parent op for pad reconstructie
                float priority = current.cost + 1 + heuristic(neighbour, target_tile);  // A* cost calculatie: G + H
                open_set.push({ neighbour, priority }); // voeg neighbor toe aan priority queue
            }
        }
    }

    return std::vector<glm::vec2>(); // Geen pad gevonden
}


bool Terrain::in_bounds(const glm::vec2& position2d) const
{
    if (position2d.x > 0.f && position2d.y > 0.f && position2d.x < terrain_width && position2d.y < terrain_length)
    {
        return true;
    }
    return false;
}

void Terrain::clamp_to_bounds(glm::vec2& position2d) const
{
    position2d.x = std::clamp(position2d.x, 0.f, terrain_width);
    position2d.y = std::clamp(position2d.y, 0.f, terrain_length);
}

/// <summary>
/// Trace back a route from target to start by following the parents in the given parent list.
/// Also, scale for tile size.
/// </summary>
std::vector<glm::vec2> Terrain::reconstruct_path(const std::unordered_map<glm::ivec2, glm::ivec2>& parents, const glm::ivec2& start_position, const glm::ivec2& target_position) const
{
    std::vector<glm::vec2> path;

    for (glm::ivec2 current = target_position; current != start_position; current = parents.at(current))
    {
        path.emplace_back(static_cast<float>(current.x) * tile_width + tile_width / 2.f, static_cast<float>(current.y) * tile_length + tile_length / 2.f);
    }

    return path;
}

/// <summary>
/// Returns a list of neighbours of a given node, if they exist and are accessible.
/// </summary>
std::vector<glm::ivec2> Terrain::get_neighbours(const glm::ivec2& node) const
{
    std::vector<glm::ivec2> neighbours;

    if (node.x > 0 && is_accessible({ node.x - 1, node.y }, node))
    {
        neighbours.push_back({ node.x - 1, node.y });
    }
    if (node.y > 0 && is_accessible({ node.x, node.y - 1 }, node))
    {
        neighbours.push_back({ node.x, node.y - 1 });
    }
    if (node.x < map_width - 1 && is_accessible({ node.x + 1, node.y }, node))
    {
        neighbours.push_back({ node.x + 1, node.y });
    }
    if (node.y < map_length - 1 && is_accessible({ node.x, node.y + 1 }, node))
    {
        neighbours.push_back({ node.x, node.y + 1 });
    }

    return neighbours;
}

/// <summary>
/// Checks if a tile is accessible from a given tile based on the height difference and tile type.
/// </summary>
bool Terrain::is_accessible(const glm::ivec2& tile, const glm::ivec2& from) const
{
    int tile_index = get_tile_index(tile.x, tile.y);
    int from_index = get_tile_index(from.x, from.y);

    if (tile_types[tile_index] == Terrain_Types::Sea || tile_types[tile_index] == Terrain_Types::Mountain)
    {
        return false;
    }

    if (terrain_heights[tile_index] - terrain_heights[from_index] > 3)
    {
        return false;
    }

    return true;
}

std::vector<Terrain::Tile_Data> Terrain::read_map_file(const std::filesystem::path& path_to_height_map, int& map_width, int& map_length) const
{
    //Four because we use all channels for our maps, some height maps use only one.
    const int channels_used = 4;
    int channels;
    int image_width;
    int image_height;

    unsigned char* image_data = stbi_load(path_to_height_map.string().c_str(), &image_width, &image_height, &channels, channels_used);

    map_width = image_width;
    map_length = image_height;

    std::vector<Tile_Data> map_tiles;
    map_tiles.reserve(image_width * image_height);
    
    std::cout << map_width << " x " << map_length << std::endl;

    for (size_t y = 0; y < image_height; y++)
    {
        for (size_t x = 0; x < image_width; x++)
        {
            int pixel_index = (y * image_width + x) * 4;

            uint32_t h1 = image_data[pixel_index + 0]; //First byte of the height
            uint32_t h2 = image_data[pixel_index + 1]; //Second byte of the height
            uint32_t tile_type = image_data[pixel_index + 2];
            uint32_t alpha = image_data[pixel_index + 3]; //Alpha, not used for now..

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

/// <summary>
/// Helper function that provides the internal vector index based on tile coordinates.
/// </summary>
int Terrain::get_tile_index(const int x, const int y) const
{
    return (y * map_width) + x;
}
