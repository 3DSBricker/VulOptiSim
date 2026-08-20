#include "pch.h"
#include "lightning.h"
#include <immintrin.h>

Lightning::Lightning() = default;

Lightning::Lightning(glm::vec3 position) : animation_timer("lightning", 0, 10, 0.25f), transform(position)
{
    transform.scale = glm::vec3(plane_size.x, plane_size.y, 1.f);
    transform.position.y += plane_size.y / 4.f;

    collision_box_min = transform.get_position2d() - glm::vec2(plane_size.x / 2, plane_size.y / 2);
    collision_box_max = transform.get_position2d() + glm::vec2(plane_size.x / 2, plane_size.y / 2);
}

void Lightning::update(const float delta_time, const Camera& camera, HeroSystem& hero_system)
{
    if (active) {
        uptime += delta_time;
        if (uptime >= lifetime) { active = false; return; }
        
        rotate_to_camera(camera);
        animation_timer.update(delta_time);
        check_hits(hero_system);
    }
}

void Lightning::check_hits(HeroSystem& hero_system) const
{
    const size_t count = hero_system.size();
    uint8_t* active_ptr = hero_system.active.data();
    const float* pos_x_ptr = hero_system.pos_x.data();
    const float* pos_z_ptr = hero_system.pos_z.data();
    const float* rad_ptr = hero_system.collision_radius.data();

    const __m256 v_min_x = _mm256_set1_ps(collision_box_min.x);
    const __m256 v_min_z = _mm256_set1_ps(collision_box_min.y);
    const __m256 v_max_x = _mm256_set1_ps(collision_box_max.x);
    const __m256 v_max_z = _mm256_set1_ps(collision_box_max.y);

    size_t i = 0;
    for (; i + 7 < count; i += 8)
    {
        const uint64_t active_bytes = *reinterpret_cast<const uint64_t*>(active_ptr + i);
        if (active_bytes == 0) continue;

        __m128i v_act_128 = _mm_cvtsi64_si128(active_bytes);
        __m256i v_act_32 = _mm256_cvtepu8_epi32(v_act_128);
        __m256 v_act_ps = _mm256_castsi256_ps(_mm256_cmpgt_epi32(v_act_32, _mm256_setzero_si256()));

        __m256 v_hx = _mm256_loadu_ps(pos_x_ptr + i);
        __m256 v_hz = _mm256_loadu_ps(pos_z_ptr + i);
        __m256 v_hr = _mm256_loadu_ps(rad_ptr + i);

        // Clamping without branches (SIMD max(min(val, max), min))
        __m256 v_clamped_x = _mm256_max_ps(v_min_x, _mm256_min_ps(v_hx, v_max_x));
        __m256 v_clamped_z = _mm256_max_ps(v_min_z, _mm256_min_ps(v_hz, v_max_z));

        __m256 v_dx = _mm256_sub_ps(v_clamped_x, v_hx);
        __m256 v_dz = _mm256_sub_ps(v_clamped_z, v_hz);
        
        __m256 v_dist_sq = _mm256_add_ps(_mm256_mul_ps(v_dx, v_dx), _mm256_mul_ps(v_dz, v_dz));
        __m256 v_r_sq = _mm256_mul_ps(v_hr, v_hr);

        __m256 v_hit = _mm256_cmp_ps(v_dist_sq, v_r_sq, _CMP_LT_OQ);
        v_hit = _mm256_and_ps(v_hit, v_act_ps);

        int mask = _mm256_movemask_ps(v_hit);
        if (mask != 0) {
            for (int j = 0; j < 8; ++j) {
                if ((mask >> j) & 1) {
                    hero_system.take_damage(i + j, damage_per_frame);
                }
            }
        }
    }

    for (; i < count; ++i) {
        if (!active_ptr[i]) continue;
        const float hx = pos_x_ptr[i];
        const float hz = pos_z_ptr[i];
        
        // Fast scalar clamp
        const float cx = (hx < collision_box_min.x) ? collision_box_min.x : ((hx > collision_box_max.x) ? collision_box_max.x : hx);
        const float cz = (hz < collision_box_min.y) ? collision_box_min.y : ((hz > collision_box_max.y) ? collision_box_max.y : hz);
        
        const float dx = cx - hx;
        const float dz = cz - hz;
        const float r = rad_ptr[i];

        if ((dx * dx) + (dz * dz) < (r * r)) { hero_system.take_damage(i, damage_per_frame); }
    }
}

void Lightning::rotate_to_camera(const Camera& camera)
{
    // Geen atan2f meer! Puur wiskunde en cross producten.
    glm::vec3 facing = camera.get_position() - transform.position;
    facing.y = 0.f; 
    
    // Voorkom normalisatie door 0
    float len_sq = facing.x * facing.x + facing.z * facing.z;
    if (len_sq > 0.0001f) {
        float inv_len = math_utils::fast_inv_sqrt(len_sq);
        glm::vec3 f = facing * inv_len;
        glm::vec3 r = glm::vec3(f.z, 0.f, -f.x); // Cross met UP (0, 1, 0)
        
        transform.rotation = glm::mat4(
            glm::vec4(r, 0.f),
            glm::vec4(0.f, 1.f, 0.f, 0.f),
            glm::vec4(f, 0.f),
            glm::vec4(0.f, 0.f, 0.f, 1.f)
        );
    }
}

void Lightning::register_draw(Sprite_Manager<Lightning>& sprite_manager) const {
    if (active) sprite_manager.register_draw(*this);
}
const glm::mat4& Lightning::get_model_matrix() const { return transform.get_matrix(); }
glm::uint32_t Lightning::get_texture_index() const { return animation_timer.get_current_frame(); }