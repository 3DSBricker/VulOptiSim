#pragma once
#include "pch.h"
#include "terrain.h"
#include "log.h"
#include <mutex>
#include <vector>
#include <algorithm>
#include <immintrin.h>

struct HeroSystem {
    // Parallelle component buffers (SoA)
    std::vector<glm::vec3> position;
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

    size_t size() const { return position.size(); }
    bool empty() const { return position.empty(); }

    void add_hero(const glm::vec3& pos, float spd, float rad, const std::vector<glm::vec2>* rt_ptr, int hp = 1000, int mp = 1000, const std::string& hero_name = "Hero") {
        position.push_back(pos);
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
        if (index >= position.size()) return;

        size_t last = position.size() - 1;
        if (index != last) {
            position[index] = position[last];
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

        position.pop_back();
        direction.pop_back();
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
        position.clear();
        direction.clear();
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
        position.reserve(capacity);
        direction.reserve(capacity);
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
        position.insert(position.end(), other.position.begin(), other.position.end());
        direction.insert(direction.end(), other.direction.begin(), other.direction.end());
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
        glm::mat4 rot(1.0f);
        rot[0][0] =  direction[i].y; 
        rot[0][2] = -direction[i].x; 
        rot[2][0] =  direction[i].x; 
        rot[2][2] =  direction[i].y;
        return glm::translate(glm::mat4(1.0f), position[i]) * rot;
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
            std::sort(pending_removes.begin(), pending_removes.end(), std::greater<size_t>());
            pending_removes.erase(std::unique(pending_removes.begin(), pending_removes.end()), pending_removes.end());

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
        if (i >= route_index.size() || i >= route_ptr.size() || i >= position.size() || !active[i]) return;

        int curr_idx = route_index[i];
        float distance = delta_time * speed[i];
        glm::vec2 pos2d(position[i].x, position[i].z);

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
                    float inv_dist = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(dist_sq)));
                    inv_dist = inv_dist * (1.5f - (0.5f * dist_sq * inv_dist * inv_dist));

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

        position[i].x = pos2d.x;
        position[i].z = pos2d.y;

        if ((i & 3) == phase) {
            position[i].y = terrain.get_height_fast(pos2d);
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