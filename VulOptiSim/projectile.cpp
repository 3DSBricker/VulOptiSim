#include "pch.h"
#include "projectile.h"
#include <immintrin.h>
#include <xmmintrin.h>
#include <cstdint>


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


#include <immintrin.h>
#include <cstdint>

#include <immintrin.h>
#include <cstdint>

void Projectile::check_collisions(HeroSystem& heroes)
{
    const float px = transform.position.x;
    const float pz = transform.position.z;
    const size_t count = heroes.size();

    const uint8_t* active_ptr = heroes.active.data();
    const float* pos_x_ptr = heroes.pos_x.data(); // Nu pure SoA arrays!
    const float* pos_z_ptr = heroes.pos_z.data(); // Nu pure SoA arrays!
    const float* rad_ptr = heroes.collision_radius.data();

    const __m256 v_px = _mm256_set1_ps(px);
    const __m256 v_pz = _mm256_set1_ps(pz);
    const __m256 v_proj_r = _mm256_set1_ps(radius);

    size_t i = 0;
    for (; i + 7 < count; i += 8)
    {
        uint64_t active_bytes = *reinterpret_cast<const uint64_t*>(active_ptr + i);
        if (active_bytes == 0) continue;

        // 2. Active mask berekenen
        __m128i v_act_128 = _mm_cvtsi64_si128(active_bytes);
        __m256i v_act_32 = _mm256_cvtepu8_epi32(v_act_128);
        __m256 v_act_ps = _mm256_castsi256_ps(_mm256_cmpgt_epi32(v_act_32, _mm256_setzero_si256()));

        // 3. RADICALE VERSNELLING: Directe 32-byte contigue loads ipv trage gathers
        __m256 v_hx = _mm256_loadu_ps(pos_x_ptr + i);
        __m256 v_hz = _mm256_loadu_ps(pos_z_ptr + i);
        __m256 v_hr = _mm256_loadu_ps(rad_ptr + i);

        __m256 v_r = _mm256_add_ps(v_proj_r, v_hr);
        __m256 v_r_sq = _mm256_mul_ps(v_r, v_r);

        // Omdat v_hx en v_hz er nu onmiddellijk zijn, stallen de onderstaande regels niet meer!
        __m256 v_dx = _mm256_sub_ps(v_hx, v_px);
        __m256 v_dz = _mm256_sub_ps(v_hz, v_pz);
        __m256 v_dist_sq = _mm256_add_ps(_mm256_mul_ps(v_dx, v_dx), _mm256_mul_ps(v_dz, v_dz));

        __m256 v_hit = _mm256_cmp_ps(v_dist_sq, v_r_sq, _CMP_LE_OQ);
        v_hit = _mm256_and_ps(v_hit, v_act_ps); 

        if (_mm256_movemask_ps(v_hit) != 0) {
            explode(heroes);
            return; 
        }
    }

    // Remainder loop
    for (; i < count; ++i)
    {
        if (!active_ptr[i]) continue;
        const float r = radius + rad_ptr[i];
        const float dx = pos_x_ptr[i] - px;
        const float dz = pos_z_ptr[i] - pz;
        if ((dx * dx + dz * dz) <= (r * r)) {
            explode(heroes);
            return;
        }
    }
}

void Projectile::explode(HeroSystem& heroes)
{
    const float px = transform.position.x;
    const float pz = transform.position.z;

    uint8_t* active_ptr = heroes.active.data();
    float* px_ptr = heroes.pos_x.data();
    float* pz_ptr = heroes.pos_z.data();
    float* rad_ptr = heroes.collision_radius.data();
    const size_t count = heroes.size();

    for (size_t i = 0; i < count; i++)
    {
        if (!active_ptr[i]) continue;

        const float r = explosion_radius + rad_ptr[i];
        const float dx = px_ptr[i] - px;
        const float dz = pz_ptr[i] - pz;

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