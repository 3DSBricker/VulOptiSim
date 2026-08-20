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

    // void sort(std::vector<int>& arr) const;
    // void quicksort(std::vector<int>& arr, int low, int high) const;

    void load_models_and_textures() const;
    void load_effects() const;
    void load_animation_effects();
    void spawn_hero_area(size_t s);
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
    bool tab_was_pressed = false;
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
        int total_cells;
        float cell_size;
        int cols, rows;
        std::vector<int> head;
        std::vector<int> head_frame; 
        std::vector<int> next;
        int current_frame = 1;
        
        // Aaneengesloten geheugenblokken beheerd door vector
        std::vector<int> cell_counts;      
        std::vector<int> cell_starts;      
        std::vector<int> dense_indices;

        Grid(float max_world_size, float size) : cell_size(size) {
            width = static_cast<int>(max_world_size / size) + 1;
            cols = width;
            rows = width;
            total_cells = cols * rows;
            
            // Allocatie van de parallelle spatial partitioning vectors
            cell_counts.resize(total_cells, 0);
            cell_starts.resize(total_cells, 0);
            
            head.assign(width * width, -1);
            head_frame.assign(width * width, 0);
            next.assign(20000, -1); 
        }
        
        inline int get_cell_id(const glm::vec2& pos) const {
            // Vang alle niet-eindige floats (NaN en Infinity) af
            if (!std::isfinite(pos.x) || !std::isfinite(pos.y)) {
                return 0; 
            }

            int c = static_cast<int>(pos.x / cell_size);
            int r = static_cast<int>(pos.y / cell_size);

            c = std::clamp(c, 0, cols - 1);
            r = std::clamp(r, 0, rows - 1);

            return r * cols + c;
        }

        inline void clear() { 
            current_frame++; 
        }
        
        // De functie op zijn juiste plek, werkend met de std::vector data pointers
        void build_parallel(const HeroSystem& heroes, ThreadPool& pool) {
            if (cell_counts.empty() || total_cells == 0) return;
            const size_t count = heroes.size();
            if (count == 0) return;
            
            // Zorg dat dense_indices genoeg ruimte heeft voor alle huidige heroes
            if (dense_indices.size() < count) {
                dense_indices.resize(count);
            }

            const uint8_t* active_mask = heroes.active.data(); 
            const float* px_ptr = heroes.pos_x.data();
            const float* pz_ptr = heroes.pos_z.data();

            // Haal de rauwe pointers op voor C-level performance
            int* counts_ptr = cell_counts.data();
            int* starts_ptr = cell_starts.data();
            int* dense_ptr = dense_indices.data();

            // PASS 1: Parallel Clear
            pool.parallel_for_chunked(total_cells, [&](size_t i, size_t chunk_id) {
                counts_ptr[i] = 0;
            });

            // PASS 2: Parallel Tellen van heroes per cel
            pool.parallel_for_chunked(count, [&](size_t i, size_t chunk_id) {
                if (active_mask[i] == 0) return;
                int cell = get_cell_id(glm::vec2(px_ptr[i], pz_ptr[i]));
                
                // Snelle atomaire increment
                _InterlockedIncrement(reinterpret_cast<volatile long*>(&counts_ptr[cell]));
            });

            // PASS 3: Prefix Sum (Sequentieel, pure cache snelheid)
            int current_offset = 0;
            for (int i = 0; i < total_cells; ++i) {
                starts_ptr[i] = current_offset;
                current_offset += counts_ptr[i];
                counts_ptr[i] = 0; // RESET direct weer naar 0 voor Pass 4
            }

            // PASS 4: Parallel Invoegen in de Dense Array
            pool.parallel_for_chunked(count, [&](size_t i, size_t chunk_id) {
                if (active_mask[i] == 0) return;
                int cell = get_cell_id(glm::vec2(px_ptr[i], pz_ptr[i]));
                
                int local_offset = _InterlockedExchangeAdd(reinterpret_cast<volatile long*>(&counts_ptr[cell]), 1);
                
                int write_index = starts_ptr[cell] + local_offset;
                dense_ptr[write_index] = static_cast<int>(i);
            });
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
