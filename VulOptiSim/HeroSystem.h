#pragma once
#include "pch.h"
#include "terrain.h"
#include "log.h"
#include <mutex>
#include <vector>
#include <algorithm>
#include <immintrin.h>

#include "algo_utils.h"
#include "math_utils.h"

struct HeroSystem {
    // Parallelle component buffers (SoA)
    std::vector<float> pos_x;
    std::vector<float> pos_y;
    std::vector<float> pos_z;
    std::vector<glm::vec2> direction;
    std::vector<float> speed;
    std::vector<int> health;
    std::vector<int> mana;
    std::vector<uint8_t> active;
    std::vector<float> collision_radius;
    std::vector<glm::vec2> force;
    std::vector<const std::vector<glm::vec2>*> route_ptr;
    std::vector<int> route_index;
    std::vector<std::string> name;

    size_t size() const { return pos_x.size(); }
    bool empty() const { return pos_x.empty(); }

    void add_hero(const glm::vec3& pos, float spd, float rad, const std::vector<glm::vec2>* rt_ptr, int hp = 1000, int mp = 1000, const std::string& hero_name = "Hero") {
        pos_x.push_back(pos.x);
        pos_y.push_back(pos.y);
        pos_z.push_back(pos.z);
        direction.push_back(glm::vec2{ 0.f, 0.f });
        speed.push_back(spd);
        health.push_back(hp);
        mana.push_back(mp);
        active.push_back(1);
        collision_radius.push_back(rad);
        force.push_back(glm::vec2{ 0.f, 0.f });
        route_ptr.push_back(rt_ptr);

        int idx = (rt_ptr && !rt_ptr->empty()) ? static_cast<int>(rt_ptr->size()) - 1 : -1;
        route_index.push_back(idx);
        name.push_back(hero_name);
    }

    void remove_hero(size_t index) {
        if (index >= pos_x.size()) return;

        size_t last = pos_x.size() - 1;
        if (index != last) {
            pos_x[index] = pos_x[last];
            pos_y[index] = pos_y[last];
            pos_z[index] = pos_z[last];
            direction[index] = direction[last];
            speed[index] = speed[last];
            health[index] = health[last];
            mana[index] = mana[last];
            active[index] = active[last];
            collision_radius[index] = collision_radius[last];
            force[index] = force[last];
            route_ptr[index] = route_ptr[last];
            route_index[index] = route_index[last];
            name[index] = std::move(name[last]);
        }

        pos_x.pop_back();
        pos_y.pop_back();
        pos_z.pop_back();        direction.pop_back();
        speed.pop_back();
        health.pop_back();
        mana.pop_back();
        active.pop_back();
        collision_radius.pop_back();
        force.pop_back();
        route_ptr.pop_back();
        route_index.pop_back();
        name.pop_back();
    }

    void clear() {
        pos_x.clear();
        pos_y.clear();
        pos_z.clear();        direction.clear();
        speed.clear();
        health.clear();
        mana.clear();
        active.clear();
        collision_radius.clear();
        force.clear();
        route_ptr.clear();
        route_index.clear();
        name.clear();
    }

    void reserve(size_t capacity) {
        pos_x.reserve(capacity);
        pos_y.reserve(capacity);
        pos_z.reserve(capacity);        direction.reserve(capacity);
        speed.reserve(capacity);
        health.reserve(capacity);
        mana.reserve(capacity);
        active.reserve(capacity);
        collision_radius.reserve(capacity);
        force.reserve(capacity);
        route_ptr.reserve(capacity);
        route_index.reserve(capacity);
        name.reserve(capacity);
    }

    void merge(const HeroSystem& other) {
        pos_x.insert(pos_x.end(), other.pos_x.begin(), other.pos_x.end());
        pos_y.insert(pos_y.end(), other.pos_y.begin(), other.pos_y.end());
        pos_z.insert(pos_z.end(), other.pos_z.begin(), other.pos_z.end());        direction.insert(direction.end(), other.direction.begin(), other.direction.end());
        speed.insert(speed.end(), other.speed.begin(), other.speed.end());
        health.insert(health.end(), other.health.begin(), other.health.end());
        mana.insert(mana.end(), other.mana.begin(), other.mana.end());
        active.insert(active.end(), other.active.begin(), other.active.end());
        collision_radius.insert(collision_radius.end(), other.collision_radius.begin(), other.collision_radius.end());
        force.insert(force.end(), other.force.begin(), other.force.end());
        route_ptr.insert(route_ptr.end(), other.route_ptr.begin(), other.route_ptr.end());
        route_index.insert(route_index.end(), other.route_index.begin(), other.route_index.end()); // FIX: Was vergeten!
        name.insert(name.end(), other.name.begin(), other.name.end());
    }
    
    glm::mat4 get_transform_matrix(size_t i) const {
        glm::mat4 m(1.0f); // Initialiseer eenmalig de identiteitsmatrix

        // Pas de rotatie toe (kolom 0 en 2)
        m[0][0] =  direction[i].y; 
        m[0][2] = -direction[i].x; 
        m[2][0] =  direction[i].x; 
        m[2][2] =  direction[i].y;

        // Pas de translatie direct toe in de 4e kolom (kolom 3 in zero-indexed)
        m[3][0] = pos_x[i];
        m[3][1] = pos_y[i];
        m[3][2] = pos_z[i];

        return m;
    }

    struct PendingSpawn {
        glm::vec3 pos;
        float speed;
        float radius;
        const std::vector<glm::vec2>* route_ptr;
    };

    std::vector<PendingSpawn> pending_adds;
    std::vector<size_t> pending_removes;
    std::mutex mutation_mutex;

    void add_hero_deferred(const glm::vec3& pos, float spd, float rad, const std::vector<glm::vec2>* rt_ptr) {
        std::lock_guard<std::mutex> lock(mutation_mutex);
        pending_adds.push_back({ pos, spd, rad, rt_ptr });
    }

    void remove_hero_deferred(size_t index) {
        std::lock_guard<std::mutex> lock(mutation_mutex);
        pending_removes.push_back(index);
    }
    
    void flush_mutations() {
        if (!pending_removes.empty()) {
            // 1. Sorteer aflopend (grootste index eerst) zonder std::sort
            algo_utils::manual_quicksort(pending_removes.data(), 0, static_cast<int>(pending_removes.size()) - 1, 
                [](size_t a, size_t b) { return a > b; });

            // 2. Verwijder duplicaten in-place zonder std::unique
            size_t write_idx = 0;
            for (size_t i = 1; i < pending_removes.size(); i++) {
                if (pending_removes[i] != pending_removes[write_idx]) {
                    write_idx++;
                    pending_removes[write_idx] = pending_removes[i];
                }
            }
            pending_removes.resize(write_idx + 1);

            for (size_t idx : pending_removes) {
                remove_hero(idx);
            }
            pending_removes.clear();
        }

        for (const auto& spawn : pending_adds) {
            add_hero(spawn.pos, spawn.speed, spawn.radius, spawn.route_ptr);
        }
        pending_adds.clear();
    }
    
    void update_hero(size_t i, float delta_time, const Terrain& terrain, uint32_t phase) {
        if (i >= route_index.size() || i >= route_ptr.size() || i >= pos_x.size() || !active[i]) return;

        int curr_idx = route_index[i];
        float distance = delta_time * speed[i];
        glm::vec2 pos2d(pos_x[i], pos_z[i]);

        pos2d += force[i];
        force[i] = glm::vec2{ 0.f, 0.f };

        const std::vector<glm::vec2>* my_route = route_ptr[i];

        if (my_route && curr_idx >= 0 && curr_idx < static_cast<int>(my_route->size())) {
            const float tile_w_sq = terrain.tile_width * terrain.tile_width;

            while (curr_idx >= 0 && distance > 0.f) {
                const glm::vec2 target = (*my_route)[curr_idx];
                const glm::vec2 target_direction = target - pos2d;
                const float dist_sq = (target_direction.x * target_direction.x) + (target_direction.y * target_direction.y);

                if (dist_sq > 0.00001f) {
                    float inv_dist = math_utils::fast_inv_sqrt(dist_sq);

                    const float dist = dist_sq * inv_dist;
                    const glm::vec2 dir = target_direction * inv_dist;

                    if (dist > distance) {
                        pos2d += distance * dir;
                        distance = 0.f;
                    } else {
                        pos2d = target;
                        distance -= dist;
                    }
                    direction[i] = dir;
                } else {
                    distance = 0.f;
                }

                const float diff_x = target.x - pos2d.x;
                const float diff_y = target.y - pos2d.y;

                if ((diff_x * diff_x + diff_y * diff_y) < tile_w_sq) {
                    curr_idx--;
                } else {
                    break;
                }
            }
            route_index[i] = curr_idx;
        }

        pos_x[i] = pos2d.x;
        pos_z[i] = pos2d.y;

        if ((i & 3) == phase) {
            pos_y[i] = terrain.get_height_fast(pos2d);
        }
    }

    void take_damage(size_t i, int damage) {
        if (!active[i]) return;
        health[i] -= damage;
        if (health[i] <= 0) {
            health[i] = 0;
            active[i] = false;
        }
    }

    void drain_mana(size_t i, int cost) {
        if (!active[i]) return;
        mana[i] -= cost;
        if (mana[i] < 0) {
            mana[i] = 0;
        }
    }
};
