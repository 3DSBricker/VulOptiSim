#include "pch.h"
#include "lightning.h"

Lightning::Lightning() = default;

Lightning::Lightning(glm::vec3 position) : animation_timer("lightning", 0, 10, 0.25f), transform(position)
{
    transform.scale = glm::vec3(plane_size.x, plane_size.y, 1.f);
    transform.position.y += plane_size.y / 4.f;

    //Set the collision box to the width of the plane (this collision box is axis-aligned)
    collision_box_min = transform.get_position2d() - glm::vec2(plane_size.x / 2, plane_size.y / 2);
    collision_box_max = transform.get_position2d() + glm::vec2(plane_size.x / 2, plane_size.y / 2);
}

void Lightning::update(const float delta_time, const Camera& camera, HeroSystem& hero_system)
{
    if (active)
    {
        uptime += delta_time;

        if (uptime >= lifetime)
        {
            active = false;
            return;
        }

        rotate_to_camera(camera);
        animation_timer.update(delta_time);

        check_hits(hero_system);
    }
}

void Lightning::register_draw(Sprite_Manager<Lightning>& sprite_manager) const
{
    if (active)
    {
        sprite_manager.register_draw(*this);
    }
}

void Lightning::check_hits(HeroSystem& hero_system) const
{
    for (size_t i = 0; i < hero_system.size(); i++)
    {
        if (hero_system.active[i])
        {
            // AABB vs Cirkel collision wiskunde:
            // Klem het midden van de held vast aan de randen van de rechthoek.
            glm::vec2 hero_pos(hero_system.position[i].x, hero_system.position[i].z);
            glm::vec2 clamped = glm::clamp(hero_pos, collision_box_min, collision_box_max);
            
            // Controleer of de afstand van de dichtstbijzijnde rand tot de kern kleiner is dan de radius
            float distance_squared = glm::length2(clamped - hero_pos);
            float r = hero_system.collision_radius[i];

            if (distance_squared < (r * r))
            {
                hero_system.take_damage(i, damage_per_frame);
            }
        }
    }
}

const glm::mat4& Lightning::get_model_matrix() const
{
    return transform.get_matrix();
}

glm::uint32_t Lightning::get_texture_index() const
{
    return animation_timer.get_current_frame();
}

void Lightning::rotate_to_camera(const Camera& camera)
{
    //Rotate so the animation is always facing the camera
    glm::vec3 facing_direction = camera.get_position() - transform.position;
    facing_direction.y = 0.f; //Only rotate the x and z
    facing_direction = glm::normalize(facing_direction);

    float angle = atan2f(facing_direction.x, facing_direction.z);

    glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0.0f, 1.0f, 0.0f));

    transform.rotation = rotation;
}
