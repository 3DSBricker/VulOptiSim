#pragma once

#include "magic_staff.h"
#include "projectile.h"
#include "sprite_manager.h"


class Scene
{
public:
    explicit Scene(vulvox::Renderer* renderer);
    
    void check_collisions();

    void update(const float delta_time);
    void draw();

    void sort(std::vector<int>& arr) const;
    void quicksort(std::vector<int>& arr, int low, int high) const;
    //std::vector<glm::vec2> convex_hull(const std::vector<glm::vec2>& points) const;

    void load_models_and_textures() const;
    void load_effects() const;
    void load_animation_effects() const; // Deferred: fireball/lightning textures
    void spawn_heroes();
    void spawn_staves();

    size_t get_character_count() const;
    size_t get_staff_count() const;
    
    const float MAX_DIST = 550.0f; // Pas aan naar wat je nodig hebt
    const float MAX_DIST_SQ = MAX_DIST * MAX_DIST;
    
    alignas(64) std::atomic<uint32_t> visible_hero_count{0};
    alignas(64) std::atomic<uint32_t> visible_staff_count{0};

private:

    //Toggle for following the character at the front
    bool follow_mode = false;
    bool show_debug_windows = false;
    bool f1_was_pressed = false;
    size_t update_frame = 0;
    size_t draw_frame = 0;
    
    struct Mat4Chunk {
        std::vector<glm::mat4> data;
        char padding[64]; // Handmatige padding ipv alignas() om MSVC heap crashes te fixen
    
        Mat4Chunk() { 
            data.reserve(5000); // Ruim pre-alloceren voorkomt her-allocaties tijdens draw
        }
    };

    void handle_input(const float delta_time);
    void show_controls();

    void show_health_values() const;
    void show_mana_values() const;

    ThreadPool pool; // initialiseer threadpool

    std::mutex hero_mutex; // anti race

    glm::dvec2 prev_mouse_pos;

    HeroSystem hero_system;
    std::vector<Magic_Staff> staves;
    std::vector<glm::mat4> hero_transforms;
    std::vector<glm::mat4> staff_transforms;
    std::vector<std::vector<glm::vec2>> collision_force_buffers;
    
    std::vector<glm::mat4> visible_staff_transforms;
    
    float LOD0_DIST2 = 100.0f * 100.0f;
    float LOD1_DIST2 = 350.0f * 350.0f;
    float LOD2_DIST2 = 600.0f * 600.0f;
    
    std::vector<Mat4Chunk> lod0_chunks;
    std::vector<Mat4Chunk> lod1_chunks;
    std::vector<Mat4Chunk> lod2_chunks;
    std::vector<std::vector<glm::mat4>> staff_chunks;
    // std::vector<std::vector<glm::mat4>> lod3_chunks;
    
    std::vector<glm::mat4> lod0;
    std::vector<glm::mat4> lod1;
    std::vector<glm::mat4> lod2;
    // std::vector<glm::mat4> lod3;
    
    std::mutex lod0_mutex;
    std::mutex lod1_mutex;
    std::mutex lod2_mutex;
    // std::mutex lod3_mutex;
    
    std::atomic<uint32_t> lod0_count{0};
    std::atomic<uint32_t> lod1_count{0};
    std::atomic<uint32_t> lod2_count{0};
    // std::atomic<uint32_t> lod3_count{0};
    std::atomic<uint32_t> staff_count_visible{0};
    
    const size_t worker_count = pool.thread_count();

    std::vector<Lightning> active_lightning;
    std::vector<Projectile> projectiles;

    Sprite_Manager<Lightning> lightning_sprite_manager = Sprite_Manager<Lightning>("lightning");
    Sprite_Manager<Projectile> projectile_sprite_manager = Sprite_Manager<Projectile>("fireball");

    int num_layers = 1;

    vulvox::Renderer* renderer;
    Camera camera;

    // Terrain terrain;
    std::unique_ptr<Terrain> terrain;

    Shield shield;

    struct Grid {
        int width;
        float cell_size;
        std::vector<int> head;
        std::vector<int> head_frame; // NIEUW: Slaat op in welke frame deze cel voor het laatst is geüpdatet
        std::vector<int> next;
        int current_frame = 1;       // NIEUW

        Grid(float max_world_size, float size) : cell_size(size) {
            width = static_cast<int>(max_world_size / size) + 1;
            head.assign(width * width, -1);
            head_frame.assign(width * width, 0);
            next.assign(20000, -1); 
        }

        inline int get_cell_id(const glm::vec2& pos) const {
            int x = static_cast<int>(std::max(0.0f, pos.x) / cell_size);
            int y = static_cast<int>(std::max(0.0f, pos.y) / cell_size);
            return x + (y * width);
        }

        void clear() { 
            current_frame++; // O(1) clear! Kost 0.000 ms.
        }

        void add_hero(int hero_index, const glm::vec2& position) {
            if (hero_index >= next.size()) next.resize(hero_index * 2, -1);
            int cell = get_cell_id(position);
        
            if(cell < head.size()) {
                if (head_frame[cell] != current_frame) {
                    head[cell] = -1; // Lazy reset van de cel als hij van een oude frame is
                    head_frame[cell] = current_frame;
                }
                next[hero_index] = head[cell];
                head[cell] = hero_index;
            }
        }
    
        // Nieuwe helper functie voor de collision logic
        int get_head(int cell) const {
            if (cell >= head.size() || head_frame[cell] != current_frame) return -1;
            return head[cell];
        }
    };

    Grid hero_grid;

    // Globale route cache voor pathfinding optimization (thread-safe)
    std::unordered_map<glm::ivec2, std::vector<glm::vec2>, IVec2Hash> global_route_cache;
    std::mutex route_cache_mutex;

    // Lazy loading voor animation textures
    mutable bool lightning_textures_loaded = false;
    mutable bool fireball_textures_loaded = false;
    mutable std::mutex animation_load_mutex;

    void ensure_animation_textures_loaded() const;

};
