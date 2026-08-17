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
    const float px = transform.position.x;
    const float pz = transform.position.z;
    uint32_t triggered = 0; // We gebruiken een integer voor branchless bitwise operaties

    const uint8_t* active_ptr = heroes.active.data();
    const glm::vec3* pos_ptr = heroes.position.data();
    const float* rad_ptr = heroes.collision_radius.data();
    const size_t count = heroes.size();

    // GEEN 'break' of 'if' statements! MSVC /O2 en /fp:fast vectoriseert dit nu genadeloos.
    for(size_t i = 0; i < count; i++)
    {
        const float r = radius + rad_ptr[i];
        const float dx = pos_ptr[i].x - px;
        const float dz = pos_ptr[i].z - pz;
        const float dist_sq = (dx * dx) + (dz * dz);

        // Branchless hit detectie: als distance <= radius, wordt het een '1'. 
        // Samen met de boolean van 'active', flippen we de triggered flag.
        triggered |= (active_ptr[i] & (dist_sq <= (r * r)));
    }

    if (triggered) {
        explode(heroes);
    }
}

void Projectile::explode(HeroSystem& heroes)
{
    const float px = transform.position.x;
    const float pz = transform.position.z;

    uint8_t* active_ptr = heroes.active.data();
    glm::vec3* pos_ptr = heroes.position.data();
    float* rad_ptr = heroes.collision_radius.data();
    const size_t count = heroes.size();

    for (size_t i = 0; i < count; i++)
    {
        if (!active_ptr[i]) continue;

        const float r = explosion_radius + rad_ptr[i];
        const float dx = pos_ptr[i].x - px;
        const float dz = pos_ptr[i].z - pz;

        if ((dx * dx + dz * dz) <= (r * r))
        {
            heroes.take_damage(i, damage); 
        }
    }
    active = false;
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