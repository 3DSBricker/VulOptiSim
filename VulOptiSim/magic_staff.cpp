#include "pch.h"
#include "magic_staff.h"
#include "math_utils.h"

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
    if (has_target && current_target_index < hero_system.size() && hero_system.active[current_target_index])
    {
        // Gebruik de positie uit de HeroSystem array
        glm::vec3 target_pos(hero_system.pos_x[current_target_index], 
                     hero_system.pos_y[current_target_index], 
                     hero_system.pos_z[current_target_index]);

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
    float closest_distance_squared = math_utils::FLT_MAX_VAL;

    const size_t count = hero_system.size();
    const uint8_t* active_ptr = hero_system.active.data();
    const float* pos_x_ptr = hero_system.pos_x.data();
    const float* pos_z_ptr = hero_system.pos_z.data();
    
    const float staff_x = transform.position.x;
    const float staff_z = transform.position.z;

    for (size_t i = 0; i < count; i++)
    {
        if (!active_ptr[i]) continue;

        // Geen vec2 / glm overhead, gewoon ruwe floats
        const float dx = pos_x_ptr[i] - staff_x;
        const float dz = pos_z_ptr[i] - staff_z;
        const float distance_squared = (dx * dx) + (dz * dz);

        if (distance_squared < closest_distance_squared)
        {
            closest_distance_squared = distance_squared;
            current_target_index = i;
            has_target = true;
        }
    }

    if (has_target) {
        if (!last_logged_target_state || current_target_index != last_logged_target_index) {
            Log::get_instance()->add_log("[Magic_Staff] %s locked on target %zu.\n", name.c_str(), current_target_index);
            last_logged_target_state = true;
            last_logged_target_index = current_target_index;
        }
    } else if (last_logged_target_state) {
        Log::get_instance()->add_log("[Magic_Staff] %s lost target lock.\n", name.c_str());
        last_logged_target_state = false;
        last_logged_target_index = std::numeric_limits<size_t>::max();
    }
}
