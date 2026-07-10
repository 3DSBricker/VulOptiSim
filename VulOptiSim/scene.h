#pragma once

#include "magic_staff.h"
#include "projectile.h"
#include "sprite_manager.h"


class Scene
{
public:
    explicit Scene(vulvox::Renderer& renderer);

    void check_collisions();

    void update(const float delta_time);
    void draw();

    void sort(std::vector<int>& arr) const;
    void quicksort(std::vector<int>& arr, int low, int high) const;
    //std::vector<glm::vec2> convex_hull(const std::vector<glm::vec2>& points) const;

    void load_models_and_textures() const;
    void load_effects() const;
    void spawn_heroes();
    void spawn_staves();

    size_t get_character_count() const;
    size_t get_staff_count() const;

private:

    //Toggle for following the character at the front
    bool follow_mode = false;
    bool show_debug_windows = false;
    bool f1_was_pressed = false;
    size_t update_frame = 0;

    void handle_input(const float delta_time);
    void show_controls();

    void show_health_values() const;
    void show_mana_values() const;

    ThreadPool pool; // initialiseer threadpool

    std::mutex hero_mutex; // anti race

    glm::dvec2 prev_mouse_pos;

    std::vector<Hero> heroes;
    std::vector<Magic_Staff> staves;
    std::vector<glm::mat4> hero_transforms;
    std::vector<glm::mat4> staff_transforms;
    std::vector<std::vector<glm::vec2>> collision_force_buffers;

    std::vector<Lightning> active_lightning;
    std::vector<Projectile> projectiles;

    Sprite_Manager<Lightning> lightning_sprite_manager = Sprite_Manager<Lightning>("lightning");
    Sprite_Manager<Projectile> projectile_sprite_manager = Sprite_Manager<Projectile>("fireball");

    int num_layers = 1;

    vulvox::Renderer* renderer;
    Camera camera;

    Terrain terrain;

    Shield shield;

    struct Grid {
        std::unordered_map<int, std::vector<int>> cells;
        float cell_size;

        Grid(float size) : cell_size(size) {}

        int get_cell_id(const glm::vec2& pos) const {
            int x = static_cast<int>(pos.x / cell_size);
            int y = static_cast<int>(pos.y / cell_size);
            return (x << 16) | y; // Unieke sleutel voor de cel
        }

        void add_hero(int hero_index, const glm::vec2& position) {
            cells[get_cell_id(position)].push_back(hero_index);
        }

        const std::vector<int>& get_nearby_heroes(const glm::vec2& position) const {
            static const std::vector<int> empty;
            auto it = cells.find(get_cell_id(position));
            if (it == cells.end())
            {
                return empty;
            }
            return it->second;
        }

        void clear() { cells.clear(); }
    };

    Grid hero_grid;


};
