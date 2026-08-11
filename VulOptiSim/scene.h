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

    void load_models_and_textures() const;
    void load_effects() const;
    void load_animation_effects() const;
    void spawn_heroes();
    void spawn_staves();

    size_t get_character_count() const;
    size_t get_staff_count() const;
    
    const float MAX_DIST = 550.0f;
    const float MAX_DIST_SQ = MAX_DIST * MAX_DIST;
    
    alignas(64) std::atomic<uint32_t> visible_hero_count{0};
    alignas(64) std::atomic<uint32_t> visible_staff_count{0};

private:
    bool follow_mode = false;
    bool show_debug_windows = false;
    bool f1_was_pressed = false;
    size_t update_frame = 0;
    size_t draw_frame = 0;
    
    struct alignas(64) Mat4Chunk {
        std::vector<glm::mat4> data; // 24 bytes
        size_t count = 0;            // 8 bytes
        // Geen handmatige char padding meer nodig! 
        // De compiler vult de overige 32 bytes automatisch perfect aan tot 64 bytes.
    
        Mat4Chunk() { 
            data.resize(5000); 
        }
    };

    void handle_input(const float delta_time);
    void show_controls();

    void show_health_values() const;
    void show_mana_values() const;

    ThreadPool pool; 

    std::mutex hero_mutex; 
    glm::dvec2 prev_mouse_pos;

    HeroSystem hero_system;
    std::vector<Magic_Staff> staves;
    std::vector<glm::mat4> hero_transforms;
    std::vector<glm::mat4> staff_transforms;
    
    std::vector<glm::mat4> visible_staff_transforms;
    
    float LOD0_DIST2 = 60.0f * 60.0f;
    float LOD1_DIST2 = 250.0f * 250.0f;
    float LOD2_DIST2 = 600.0f * 600.0f;
    
    std::vector<Mat4Chunk> lod0_chunks;
    std::vector<Mat4Chunk> lod1_chunks;
    std::vector<Mat4Chunk> lod2_chunks;
    std::vector<Mat4Chunk> staff_chunks;
    
    std::vector<glm::mat4> lod0;
    std::vector<glm::mat4> lod1;
    std::vector<glm::mat4> lod2;
    
    std::mutex lod0_mutex;
    std::mutex lod1_mutex;
    std::mutex lod2_mutex;
    
    std::atomic<uint32_t> lod0_count{0};
    std::atomic<uint32_t> lod1_count{0};
    std::atomic<uint32_t> lod2_count{0};
    std::atomic<uint32_t> staff_count_visible{0};
    
    const size_t worker_count = pool.thread_count();

    std::vector<Lightning> active_lightning;
    std::vector<Projectile> projectiles;

    Sprite_Manager<Lightning> lightning_sprite_manager = Sprite_Manager<Lightning>("lightning");
    Sprite_Manager<Projectile> projectile_sprite_manager = Sprite_Manager<Projectile>("fireball");

    int num_layers = 1;

    vulvox::Renderer* renderer;
    Camera camera;

    std::unique_ptr<Terrain> terrain;
    Shield shield;

    struct Grid {
        int width;
        float cell_size;
        std::vector<int> head;
        std::vector<int> head_frame; 
        std::vector<int> next;
        int current_frame = 1;       

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

        inline void clear() { 
            current_frame++; 
        }

        void add_hero(int hero_index, const glm::vec2& position) {
            if (hero_index >= next.size()) next.resize(hero_index * 2, -1);
            int cell = get_cell_id(position);
        
            if(cell < head.size()) {
                if (head_frame[cell] != current_frame) {
                    head[cell] = -1; 
                    head_frame[cell] = current_frame;
                }
                next[hero_index] = head[cell];
                head[cell] = hero_index;
            }
        }
    
        inline int get_head(int cell) const {
            if (cell >= head.size() || head_frame[cell] != current_frame) return -1;
            return head[cell];
        }
    };

    Grid hero_grid;

    std::unordered_map<glm::ivec2, std::vector<glm::vec2>, IVec2Hash> global_route_cache;
    std::mutex route_cache_mutex;

    mutable bool lightning_textures_loaded = false;
    mutable bool fireball_textures_loaded = false;
    mutable std::mutex animation_load_mutex;

    void ensure_animation_textures_loaded() const;
};