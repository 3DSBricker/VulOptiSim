#pragma once
#include "pch.h"
#include "terrain.h"
#include "log.h"

struct HeroSystem {
    // Parallelle component buffers (SoA)
    std::vector<glm::vec3> position;
    std::vector<glm::mat4> rotation;
    std::vector<float> speed;
    std::vector<int> health;
    std::vector<int> mana;
    std::vector<bool> active;
    std::vector<float> collision_radius;
    std::vector<glm::vec2> force;
    std::vector<std::vector<glm::vec2>> route;
    std::vector<std::string> name;

    size_t size() const { return position.size(); }
    bool empty() const { return position.empty(); }

    // Helpers voor initialisatie
    void add_hero(const glm::vec3& pos, float spd, float radius, const std::vector<glm::vec2>& rt) {
        position.push_back(pos);
        rotation.push_back(glm::mat4(1.0f));
        speed.push_back(spd);
        health.push_back(1000);
        mana.push_back(1000);
        active.push_back(true);
        collision_radius.push_back(radius);
        force.push_back(glm::vec2(0.f));
        route.push_back(rt);
        name.push_back("Hero"); // Of een specifieke naam indien nodig
    }

    void merge(const HeroSystem& other) {
        position.insert(position.end(), other.position.begin(), other.position.end());
        rotation.insert(rotation.end(), other.rotation.begin(), other.rotation.end());
        speed.insert(speed.end(), other.speed.begin(), other.speed.end());
        health.insert(health.end(), other.health.begin(), other.health.end());
        mana.insert(mana.end(), other.mana.begin(), other.mana.end());
        active.insert(active.end(), other.active.begin(), other.active.end());
        collision_radius.insert(collision_radius.end(), other.collision_radius.begin(), other.collision_radius.end());
        force.insert(force.end(), other.force.begin(), other.force.end());
        route.insert(route.end(), other.route.begin(), other.route.end());
        name.insert(name.end(), other.name.begin(), other.name.end());
    }

    void remove_hero(size_t index) {
        size_t last = position.size() - 1;
        if (index != last) {
            position[index] = std::move(position[last]);
            rotation[index] = std::move(rotation[last]);
            speed[index] = speed[last];
            health[index] = health[last];
            mana[index] = mana[last];
            active[index] = active[last];
            collision_radius[index] = collision_radius[last];
            force[index] = std::move(force[last]);
            route[index] = std::move(route[last]);
            name[index] = std::move(name[last]);
        }
        position.pop_back(); rotation.pop_back(); speed.pop_back(); health.pop_back();
        mana.pop_back(); active.pop_back(); collision_radius.pop_back(); force.pop_back();
        route.pop_back(); name.pop_back();
    }

    glm::mat4 get_transform_matrix(size_t i) const {
        return glm::translate(glm::mat4(1.0f), position[i]) * rotation[i];
    }

    // --- LOGICA VANUIT DE OUDE Hero CLASS ---

    void update_hero(size_t i, float delta_time, const Terrain& terrain) {
        if (!active[i]) return;

        float distance = delta_time * speed[i];
        glm::vec2 pos2d(position[i].x, position[i].z);

        pos2d += force[i];
        force[i] = glm::vec2{ 0.f, 0.f };

        if (!route[i].empty()) {
            while (!route[i].empty() && distance > 0.f) {
                glm::vec2 target = route[i].back();
                glm::vec2 target_direction = target - pos2d;
                float distance_to_target = glm::length(target_direction);

                if (distance_to_target > 0.00001f) {
                    glm::vec2 dir = target_direction / distance_to_target;

                    if (distance_to_target > distance) {
                        pos2d += distance * dir;
                        distance = 0.f;
                    } else {
                        pos2d = target;
                        distance -= distance_to_target;
                    }

                    // Face target (rotatie opslaan in de array)
                    // (0 wiskunde, enkel in het geheugen prikken):
                    rotation[i] = glm::mat4(1.0f);
                    rotation[i][0][0] =  dir.y; 
                    rotation[i][0][2] = -dir.x; 
                    rotation[i][2][0] =  dir.x; 
                    rotation[i][2][2] =  dir.y;
                    
                }
                
                if (glm::length2(target - pos2d) < terrain.tile_width) {
                    route[i].pop_back();
                }
            }
        }

        if (terrain.in_bounds(pos2d)) {
            position[i].x = pos2d.x;
            position[i].z = pos2d.y;
        } else {
            glm::vec2 clamped = pos2d;
            terrain.clamp_to_bounds(clamped);
            position[i].x = clamped.x;
            position[i].z = clamped.y;
        }

        position[i].y = terrain.get_height_fast(glm::vec2(position[i].x, position[i].z));
    }

    void take_damage(size_t i, int damage) {
        if (!active[i]) return;
        health[i] -= damage;
        Log::get_instance()->add_log("%s takes %d damage.\n", name[i].c_str(), damage);
        if (health[i] <= 0) {
            health[i] = 0;
            active[i] = false;
            Log::get_instance()->add_log("%s is down for the count!\n", name[i].c_str());
        }
    }

    void drain_mana(size_t i, int cost) {
        mana[i] -= cost;
        Log::get_instance()->add_log("%s loses %d mana.\n", name[i].c_str(), cost);
        if (mana[i] < 0) {
            mana[i] = 0;
            Log::get_instance()->add_log("%s is oom, weakening the shield!\n", name[i].c_str());
        }
    }
};