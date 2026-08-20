#include "pch.h"
#include "shield.h"
#include "thread_pool.h"


Shield::Shield(const std::string& texture_array_name)
    : texture_name(texture_array_name)
{

}

struct ExtremaChunk {
    glm::vec2 p_min_x{FLT_MAX, 0}, p_max_x{-FLT_MAX, 0};
    glm::vec2 p_min_y{0, FLT_MAX}, p_max_y{0, -FLT_MAX};
    glm::vec2 p_min_sum{0, 0}, p_max_sum{0, 0};
    glm::vec2 p_min_diff{0, 0}, p_max_diff{0, 0};
    float min_sum_val = FLT_MAX, max_sum_val = -FLT_MAX;
    float min_diff_val = FLT_MAX, max_diff_val = -FLT_MAX;
    float lowest_point = FLT_MAX, highest_point = -FLT_MAX;
    glm::vec2 min_bounds{FLT_MAX};
    glm::vec2 max_bounds{-FLT_MAX};
    bool has_points = false;
};

void Shield::update(const HeroSystem& hero_system, ThreadPool* pool)
{
    const size_t count = hero_system.size();
    if (count == 0) {
        convex_hull_points.clear();
        if (last_logged_hull_size != 0) {
            Log::get_instance()->add_log("[Shield] No heroes available; shield hull cleared.\n");
            last_logged_hull_size = 0;
        }
        return;
    }

    const uint8_t* active_ptr = hero_system.active.data();
    const int* mana_ptr = hero_system.mana.data();
    const float* pos_x_ptr = hero_system.pos_x.data();
    const float* pos_y_ptr = hero_system.pos_y.data();
    const float* pos_z_ptr = hero_system.pos_z.data();

    ExtremaChunk global_extrema;

    if (pool && count > 1000) {
        size_t thread_count = pool->thread_count();
        const size_t target_chunks = thread_count * 8; // Match de factor uit de threadpool!
        const size_t chunk_size = std::max<size_t>(1, count / target_chunks);
        const size_t total_chunks = (count + chunk_size - 1) / chunk_size;

        std::vector<ExtremaChunk> chunks(total_chunks); // Safe voor elke chunk_id!

        pool->parallel_for_chunked(count, [&](size_t i, size_t chunk_id) {
            if (active_ptr[i] && mana_ptr[i] > 0) {
                auto& c = chunks[chunk_id];
                c.has_points = true;
                glm::vec2 p(pos_x_ptr[i], pos_z_ptr[i]);
                float sum = p.x + p.y;
                float diff = p.x - p.y;
                float y = pos_y_ptr[i];

                if (p.x < c.p_min_x.x) c.p_min_x = p;
                if (p.x > c.p_max_x.x) c.p_max_x = p;
                if (p.y < c.p_min_y.y) c.p_min_y = p;
                if (p.y > c.p_max_y.y) c.p_max_y = p;
                
                if (sum < c.min_sum_val) { c.min_sum_val = sum; c.p_min_sum = p; }
                if (sum > c.max_sum_val) { c.max_sum_val = sum; c.p_max_sum = p; }
                
                if (diff < c.min_diff_val) { c.min_diff_val = diff; c.p_min_diff = p; }
                if (diff > c.max_diff_val) { c.max_diff_val = diff; c.p_max_diff = p; }

                if (p.x < c.min_bounds.x) c.min_bounds.x = p.x;
                if (p.x > c.max_bounds.x) c.max_bounds.x = p.x;
                if (p.y < c.min_bounds.y) c.min_bounds.y = p.y;
                if (p.y > c.max_bounds.y) c.max_bounds.y = p.y;

                if (y < c.lowest_point) c.lowest_point = y;
                if (y > c.highest_point) c.highest_point = y;
            }
        });

        // Merge de resultaten van alle threads
        for (const auto& c : chunks) {
            if (!c.has_points) continue;
            global_extrema.has_points = true;
            if (c.p_min_x.x < global_extrema.p_min_x.x) global_extrema.p_min_x = c.p_min_x;
            if (c.p_max_x.x > global_extrema.p_max_x.x) global_extrema.p_max_x = c.p_max_x;
            if (c.p_min_y.y < global_extrema.p_min_y.y) global_extrema.p_min_y = c.p_min_y;
            if (c.p_max_y.y > global_extrema.p_max_y.y) global_extrema.p_max_y = c.p_max_y;

            if (c.min_sum_val < global_extrema.min_sum_val) { global_extrema.min_sum_val = c.min_sum_val; global_extrema.p_min_sum = c.p_min_sum; }
            if (c.max_sum_val > global_extrema.max_sum_val) { global_extrema.max_sum_val = c.max_sum_val; global_extrema.p_max_sum = c.p_max_sum; }
            if (c.min_diff_val < global_extrema.min_diff_val) { global_extrema.min_diff_val = c.min_diff_val; global_extrema.p_min_diff = c.p_min_diff; }
            if (c.max_diff_val > global_extrema.max_diff_val) { global_extrema.max_diff_val = c.max_diff_val; global_extrema.p_max_diff = c.p_max_diff; }

            if (c.min_bounds.x < global_extrema.min_bounds.x) global_extrema.min_bounds.x = c.min_bounds.x;
            if (c.max_bounds.x > global_extrema.max_bounds.x) global_extrema.max_bounds.x = c.max_bounds.x;
            if (c.min_bounds.y < global_extrema.min_bounds.y) global_extrema.min_bounds.y = c.min_bounds.y;
            if (c.max_bounds.y > global_extrema.max_bounds.y) global_extrema.max_bounds.y = c.max_bounds.y;

            if (c.lowest_point < global_extrema.lowest_point) global_extrema.lowest_point = c.lowest_point;
            if (c.highest_point > global_extrema.highest_point) global_extrema.highest_point = c.highest_point;
        }
    } else {
        // Seriële fallback
        for (size_t i = 0; i < count; ++i) {
            if (active_ptr[i] && mana_ptr[i] > 0) {
                global_extrema.has_points = true;
                glm::vec2 p(pos_x_ptr[i], pos_z_ptr[i]);
                float sum = p.x + p.y;
                float diff = p.x - p.y;
                float y = pos_y_ptr[i];

                if (p.x < global_extrema.p_min_x.x) global_extrema.p_min_x = p;
                if (p.x > global_extrema.p_max_x.x) global_extrema.p_max_x = p;
                if (p.y < global_extrema.p_min_y.y) global_extrema.p_min_y = p;
                if (p.y > global_extrema.p_max_y.y) global_extrema.p_max_y = p;
                
                if (sum < global_extrema.min_sum_val) { global_extrema.min_sum_val = sum; global_extrema.p_min_sum = p; }
                if (sum > global_extrema.max_sum_val) { global_extrema.max_sum_val = sum; global_extrema.p_max_sum = p; }
                if (diff < global_extrema.min_diff_val) { global_extrema.min_diff_val = diff; global_extrema.p_min_diff = p; }
                if (diff > global_extrema.max_diff_val) { global_extrema.max_diff_val = diff; global_extrema.p_max_diff = p; }

                if (p.x < global_extrema.min_bounds.x) global_extrema.min_bounds.x = p.x;
                if (p.x > global_extrema.max_bounds.x) global_extrema.max_bounds.x = p.x;
                if (p.y < global_extrema.min_bounds.y) global_extrema.min_bounds.y = p.y;
                if (p.y > global_extrema.max_bounds.y) global_extrema.max_bounds.y = p.y;

                if (y < global_extrema.lowest_point) global_extrema.lowest_point = y;
                if (y > global_extrema.highest_point) global_extrema.highest_point = y;
            }
        }
    }

    if (!global_extrema.has_points) {
        convex_hull_points.clear();
        if (last_logged_hull_size != 0) {
            Log::get_instance()->add_log("[Shield] No valid shield points found; hull cleared.\n");
            last_logged_hull_size = 0;
        }
        return;
    }

    min_height = global_extrema.lowest_point;
    max_height = global_extrema.highest_point;
    min_bounds = global_extrema.min_bounds;
    max_bounds = global_extrema.max_bounds;

    std::vector<glm::vec2> extreme_points;
    extreme_points.reserve(8);
    auto add_unique = [&](const glm::vec2& pt) {
        for (const auto& existing : extreme_points) {
            if (math_utils::abs_diff(existing.x, pt.x) < 0.1f && glm::abs(existing.y - pt.y) < 0.1f) return;
        }
        extreme_points.push_back(pt);
    };

    add_unique(global_extrema.p_min_x); add_unique(global_extrema.p_max_x);
    add_unique(global_extrema.p_min_y); add_unique(global_extrema.p_max_y);
    add_unique(global_extrema.p_min_sum); add_unique(global_extrema.p_max_sum);
    add_unique(global_extrema.p_min_diff); add_unique(global_extrema.p_max_diff);

    convex_hull_points = convex_hull(extreme_points);

    if (convex_hull_points.size() > 1) {
        grow_from_centroid();
    }

    min_bounds -= glm::vec2(2.f);
    max_bounds += glm::vec2(2.f);

    if (convex_hull_points.size() != last_logged_hull_size) {
        Log::get_instance()->add_log(
            "[Shield] Hull updated: %zu edges, height range [%f, %f].\n",
            convex_hull_points.size(),
            min_height,
            max_height
        );
        last_logged_hull_size = convex_hull_points.size();
    }
}


std::vector<glm::vec2> Shield::convex_hull(std::vector<glm::vec2> all_points) const
{
    if (all_points.size() <= 3) return all_points;

    // 1. Handmatige sortering
    algo_utils::manual_quicksort(all_points.data(), 0, static_cast<int>(all_points.size()) - 1, 
        [](const glm::vec2& a, const glm::vec2& b) {
            return a.x < b.x || (a.x == b.x && a.y < b.y);
        });

    // 2. Handmatige deduplicatie
    size_t write_idx = 0;
    for (size_t i = 1; i < all_points.size(); i++) {
        if (glm::abs(all_points[i].x - all_points[write_idx].x) > 0.001f || 
            glm::abs(all_points[i].y - all_points[write_idx].y) > 0.001f) {
            write_idx++;
            all_points[write_idx] = all_points[i];
            }
    }
    all_points.resize(write_idx + 1);
    
    // 3. Monotone Chain Algorithm
    std::vector<glm::vec2> hull;
    hull.reserve(all_points.size());

    auto cross = [](const glm::vec2& o, const glm::vec2& a, const glm::vec2& b) {
        return (a.x - o.x) * (b.y - o.y) - (a.y - o.y) * (b.x - o.x);
    };

    // Lower hull
    for (const auto& pt : all_points) {
        while (hull.size() >= 2 && cross(hull[hull.size() - 2], hull.back(), pt) <= 0.0f) {
            hull.pop_back();
        }
        hull.push_back(pt);
    }

    // Upper hull
    size_t t = hull.size() + 1;
    for (auto it = all_points.rbegin() + 1; it != all_points.rend(); ++it) {
        while (hull.size() >= t && cross(hull[hull.size() - 2], hull.back(), *it) <= 0.0f) {
            hull.pop_back();
        }
        hull.push_back(*it);
    }

    hull.pop_back(); // Verwijder het laatste stipje omdat dit een kopie van het eerste is
    return hull;
}

void Shield::draw(vulvox::Renderer* renderer) const
{
    if (convex_hull_points.size() <= 1)
    {
        return;
    }

    std::vector<glm::mat4> transforms;
    transforms.reserve(convex_hull_points.size());

    std::vector<uint32_t> texture_indices(convex_hull_points.size(), 0);

    std::vector<glm::vec4> uvs;
    uvs.reserve(convex_hull_points.size());

    //Place the shield planes between each consecutive two points in the convex hull
    float shield_start = 0.f;
    for (size_t i = 0; i < convex_hull_points.size(); i++)
    {
        size_t next_index = (i + 1) % convex_hull_points.size();

        //Calculate shield plane position, rotation, and size
        //https://www.geogebra.org/3d/n2w444tn
        glm::vec2 position = convex_hull_points.at(i); //Position is based on bottom left corner
        glm::vec2 distance_vec = convex_hull_points.at(next_index) - convex_hull_points.at(i);

        //Get outward vector by calculating the perpendicular vector
        glm::vec2 line_vec = glm::normalize(convex_hull_points.at(next_index) - convex_hull_points.at(i));

        glm::mat4 rotation{ 1.0f };
        rotation[0] = glm::vec4(line_vec.x, 0.f, line_vec.y, 0.f); //right
        rotation[1] = glm::vec4(0.f, 1.f, 0.f, 0.f); //up
        rotation[2] = glm::vec4(line_vec.y, 0.f, -line_vec.x, 0.f); //forward

        float shield_length = glm::length(distance_vec);

        //Move the plane in between the two points
        glm::mat4 translate = glm::translate(glm::mat4(1.f), glm::vec3(position.x, min_height + ((max_height + shield_height) - min_height) / 2, position.y) + glm::vec3(distance_vec.x, 0.f, distance_vec.y) / 2.f);
        glm::mat4 scale = glm::scale(glm::mat4(1.f), glm::vec3(shield_length, (max_height + shield_height) - min_height, 1));
        glm::mat4 model_matrix = translate * rotation * scale;

        transforms.push_back(model_matrix);

        uvs.emplace_back(glm::vec4{ shield_start / shield_texture_scalar, 0.f, (shield_start + shield_length) / shield_texture_scalar, ((max_height + shield_height) - min_height) / shield_texture_scalar });

        shield_start += shield_length;
    }

    renderer->draw_planes(texture_name, transforms, texture_indices, uvs);
}

void Shield::absorb(HeroSystem& hero_system, glm::vec2 point) const
{
    std::vector<size_t> closest_heroes;
    std::vector<float> closest_distances(n_to_sustain, math_utils::FLT_MAX_VAL);

    const size_t count = hero_system.size();
    const uint8_t* active_ptr = hero_system.active.data();
    const float* px_ptr = hero_system.pos_x.data();
    const float* pz_ptr = hero_system.pos_z.data();

    for (size_t i = 0; i < count; i++)
    {
        if (!active_ptr[i]) continue;

        // Geen glm::length2, pure scalars
        const float dx = px_ptr[i] - point.x;
        const float dz = pz_ptr[i] - point.y;
        const float distance_squared = (dx * dx) + (dz * dz);

        if (closest_heroes.size() < n_to_sustain)
        {
            closest_heroes.push_back(i);
            closest_distances[closest_heroes.size() - 1] = distance_squared;
        }
        else
        {
            size_t farthest = 0;
            float max_dist = closest_distances[0];
            
            // Loop unrolling for kleine n_to_sustain als dit een bekende kleine const is, anders:
            for (size_t j = 1; j < closest_heroes.size(); j++) {
                if (max_dist < closest_distances[j]) {
                    farthest = j;
                    max_dist = closest_distances[j];
                }
            }

            if (distance_squared < max_dist)
            {
                closest_heroes[farthest] = i;
                closest_distances[farthest] = distance_squared;
            }
        }
    }
    
    Log::get_instance()->add_log("[Shield] Absorbed mana from %zu closest heroes.\n", closest_heroes.size());
    for (size_t index : closest_heroes)
    {
        hero_system.drain_mana(index, mana_cost);
    }
}

bool Shield::intersects(const glm::vec2& circle_center, float radius) const
{
    if (convex_hull_points.size() < 2) return false;

    if (circle_center.x + radius < min_bounds.x || circle_center.x - radius > max_bounds.x ||
        circle_center.y + radius < min_bounds.y || circle_center.y - radius > max_bounds.y) {
        return false; 
    }

    const float rad_sq = radius * radius;

    for (size_t i = 0; i < convex_hull_points.size(); i++)
    {
        glm::vec2 A = convex_hull_points[i];
        glm::vec2 B = convex_hull_points[(i + 1) % convex_hull_points.size()];

        float ab_x = B.x - A.x;
        float ab_y = B.y - A.y;
        
        float t = ((circle_center.x - A.x) * ab_x + (circle_center.y - A.y) * ab_y) / (ab_x * ab_x + ab_y * ab_y);
        t = math_utils::clamp(t, 0.0f, 1.0f); // Fast math_utils clamp ipv std/glm

        float closest_x = A.x + t * ab_x;
        float closest_y = A.y + t * ab_y;
        
        float diff_x = circle_center.x - closest_x;
        float diff_y = circle_center.y - closest_y;

        if ((diff_x * diff_x + diff_y * diff_y) <= rad_sq)
        {
            return true;
        }
    }
    return false;
}

/// <summary>
/// Calculate the center (centroid) of a set of points.
/// </summary>
glm::vec2 Shield::calculate_centroid()
{
    glm::vec2 sum(0.0f, 0.0f);
    for (const auto& convex_point : convex_hull_points) {
        sum += convex_point;
    }
    return sum / static_cast<float>(convex_hull_points.size());
}

void Shield::grow_from_centroid()
{
    //Push out the convex hull away from its centroid by a unit vector
    glm::vec2 centroid = calculate_centroid();

    for (auto& convex_point : convex_hull_points)
    {
        glm::vec2 grow_vector = glm::normalize(convex_point - centroid);
        convex_point += 2.f * grow_vector;
    }
}
