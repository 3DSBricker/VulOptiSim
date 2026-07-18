#include "pch.h"
#include "magic_staff.h"

Magic_Staff::Magic_Staff(const glm::vec3& position, const Terrain* terrain) : name("staff"), transform(position), terrain(terrain)
{
    transform.scale = glm::vec3(0.5f);
}

void Magic_Staff::update(const float delta_time, HeroSystem& hero_system, std::vector<Lightning>& active_lightning, std::vector<Projectile>& active_projectiles)
{
    current_lightning_cooldown += delta_time;
    current_shoot_cooldown += delta_time;

    target_check_timer -= delta_time;

    if (target_check_timer <= 0.0f)
    {
        target_check_timer = target_check_interval;

        // Bepaal of de huidige index nog geldig/actief is.
        // Zo niet, zoek een nieuwe.
        if (!has_target || current_target_index >= hero_system.size() || !hero_system.active[current_target_index])
        {
            find_closest_target(hero_system);
        }
    }

    if (current_lightning_cooldown >= lightning_cooldown)
    {
        current_lightning_cooldown -= lightning_cooldown;
        spawn_lightning(active_lightning);
    }

    if (current_shoot_cooldown >= shoot_cooldown)
    {
        current_shoot_cooldown -= shoot_cooldown;
        spawn_projectile(hero_system, active_projectiles);
    }
}

void Magic_Staff::draw(vulvox::Renderer* renderer) const
{
    renderer->draw_model("staff", "staff", transform.get_matrix());
}

void Magic_Staff::spawn_lightning(std::vector<Lightning>& active_lightning) const
{
    glm::vec2 staff_position = transform.get_position2d();

    //Spawn two lightning storms in a line under the staff
    for (int i = 0; i < 2; i++)
    {
        glm::vec2 lightning_position_2d = staff_position;
        lightning_position_2d.y += (float)i * (terrain->tile_length * 20.f);

        float height = terrain->get_height(lightning_position_2d);

        glm::vec3 lightning_position{ lightning_position_2d.x, height, lightning_position_2d.y };

        active_lightning.emplace_back(lightning_position);
    }

    Log::get_instance()->add_log("%s casts lightning storm!\n", name);
}

void Magic_Staff::spawn_projectile(HeroSystem& hero_system, std::vector<Projectile>& active_projectiles)
{
    if (has_target && current_target_index < hero_system.size())
    {
        // Gebruik de positie uit de HeroSystem array
        glm::vec3 target_pos = hero_system.position[current_target_index];

        active_projectiles.emplace_back(
            transform.position,
            target_pos // <-- Geef hier de vec3 positie mee
        );

        Log::get_instance()->add_log(
            "%s shoots a missile at target index %zu.\n",
            name.c_str(),
            current_target_index
        );
    }
}

void Magic_Staff::find_closest_target(const HeroSystem& hero_system)
{
    has_target = false;
    float closest_distance_squared = std::numeric_limits<float>::max();

    for (size_t i = 0; i < hero_system.size(); i++)
    {
        if (!hero_system.active[i]) continue;

        glm::vec2 pos2d(hero_system.position[i].x, hero_system.position[i].z);
        float distance_squared = glm::length2(pos2d - transform.get_position2d());

        if (distance_squared < closest_distance_squared)
        {
            closest_distance_squared = distance_squared;
            current_target_index = i;
            has_target = true;
        }
    }
}
