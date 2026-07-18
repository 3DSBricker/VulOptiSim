#include "pch.h"
#include "projectile.h"

Projectile::Projectile()
{
}

Projectile::Projectile(glm::vec3 spawn_position, glm::vec3 target_position) 
    : transform(spawn_position), animation_timer("fireball", 0, 33, 0.1f)
{
    transform.scale = glm::vec3(10.f);
    
    // Voorkom division by zero als target exact op spawn staat
    glm::vec3 diff = target_position - spawn_position;
    if(glm::length(diff) > 0.0001f) {
        direction = glm::normalize(diff);
    } else {
        direction = glm::vec3(1.f, 0.f, 0.f);
    }
}

void Projectile::update(
    const float delta_time,
    const Camera& camera,
    const Shield& shield,
    HeroSystem& heroes) // <-- Aangepast
{
    if(!active)
        return;

    uptime += delta_time;

    if(uptime >= lifetime)
    {
        active=false;
        return;
    }

    transform.position += direction * speed * delta_time;

    rotate_to_camera(camera);

    animation_timer.update(delta_time);

    if(shield.intersects(transform.get_position2d(), radius))
    {
        shield.absorb(heroes, transform.get_position2d());
        active=false;
        return;
    }

    check_collisions(heroes);
}

void Projectile::check_collisions(HeroSystem& heroes)
{
    // Door alle actieve heroes itereren
    for(size_t i = 0; i < heroes.size(); i++)
    {
        if(!heroes.active[i])
            continue;

        const glm::vec3& hp = heroes.position[i];
        const float r = radius + heroes.collision_radius[i];

        // 1. Snelle AABB check (Bounding Box)
        if(std::abs(hp.x - transform.position.x) > r ||
           std::abs(hp.z - transform.position.z) > r)
        {
            continue;
        }

        // 2. Precieze cirkel collision check (squared distance)
        float dx = hp.x - transform.position.x;
        float dz = hp.z - transform.position.z;
        if((dx * dx + dz * dz) <= (r * r))
        {
            explode(heroes);
            return;
        }
    }
}

void Projectile::explode(HeroSystem& heroes)
{
    for (size_t i = 0; i < heroes.size(); i++)
    {
        if (!heroes.active[i]) continue;

        float r = explosion_radius + heroes.collision_radius[i];
        float dx = heroes.position[i].x - transform.position.x;
        float dz = heroes.position[i].z - transform.position.z;

        // Als ze in de explosion radius zijn:
        if ((dx * dx + dz * dz) <= (r * r))
        {
            // Ga ervan uit dat je dit in HeroSystem hebt, 
            // of doe direct: heroes.health[i] -= damage;
            heroes.take_damage(i, damage); 
        }
    }

    active = false;

    //TODO: Explode
}

void Projectile::register_draw(Sprite_Manager<Projectile>& sprite_manager) const
{
    if (active)
    {
        sprite_manager.register_draw(*this);
    }
}

const glm::mat4& Projectile::get_model_matrix() const
{
    return transform.get_matrix();
}

glm::uint32_t Projectile::get_texture_index() const
{
    return animation_timer.get_current_frame();
}


void Projectile::rotate_to_camera(const Camera& camera)
{
    ////Rotate so the animation is always facing the camera
    glm::vec3 projectile_direction = glm::normalize(direction);
    glm::vec3 rot_axis = glm::normalize(glm::cross(glm::vec3(0, 1, 0), projectile_direction));
    float angle = acosf(glm::dot(glm::vec3(0, 1, 0), projectile_direction));

    glm::mat4 rotate_to_target = glm::rotate(glm::mat4(1.0f), angle, rot_axis);

    glm::vec3 normal_vec = rotate_to_target * glm::vec4(0.f, 0.f, 1.f, 0.f);
    glm::vec3 camera_direction = glm::normalize(camera.get_position() - transform.position);

    float camera_angle = acosf(glm::dot(normal_vec, camera_direction));

    glm::mat4 rotate_to_camera = glm::rotate(glm::mat4(1.0f), camera_angle, projectile_direction);


    transform.rotation = rotate_to_camera * rotate_to_target;



    //glm::vec3 facing_direction = glm::normalize(camera.get_position() - transform.position);



    //glm::vec3 facing_direction = glm::normalize(camera.get_position() - transform.position);

    //glm::vec3 rotation_axis = glm::normalize(direction);

    //float angle = acosf(glm::dot(glm::vec3(0, 1, 0), facing_direction));

    //glm::mat4 rotation = glm::rotate(glm::mat4(1.0f), angle, rotation_axis);

    //transform.rotation = rotation;

}