#include "pch.h"
#include "scene.h"
#include <immintrin.h> // VERPLICHT voor _mm_rsqrt_ss

#include "math_utils.h"
#include "algo_utils.h"

Scene::Scene(vulvox::Renderer* renderer) : renderer(renderer), terrain(std::make_unique<Terrain>(TERRAIN_PATH)),
                                           pool(math_utils::max(1u, std::thread::hardware_concurrency())),
                                           hero_grid(10000.0f, 8.0f) 
{
    auto total_start = std::chrono::high_resolution_clock::now();
    Log::get_instance()->add_log("[Scene] Constructing scene with %zu worker threads.\n", pool.thread_count());
    glfwGetCursorPos(this->renderer->get_window(), &prev_mouse_pos.x, &prev_mouse_pos.y);

    glm::vec3 camera_pos{ -28.2815380f, 305.485260f, -30.0800228f };
    glm::vec3 camera_up{ 0.338442326f, 0.869414926f, 0.359964609f };
    glm::vec3 camera_direction{ 0.595541596f, -0.494082689f, 0.633413374f };

    camera = Camera(camera_pos, camera_up, camera_direction, 100.f, 100.f);

    auto terrain_start = std::chrono::high_resolution_clock::now();
    terrain->initialize(renderer);
    auto terrain_end = std::chrono::high_resolution_clock::now();
    float terrain_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(terrain_end - terrain_start).count();
    Log::get_instance()->add_log("[Scene] Terrain initialized in %f ms.\n", terrain_duration);

    shield = Shield{ "shield" };
    Log::get_instance()->add_log("[Scene] Shield system created.\n");

    // 1. Reserveer ruimte voor alle top-level taken (6 laadtaken + 10 hero gebieden)
    std::vector<std::future<void>> futures;
    futures.reserve(16);

    futures.push_back(pool.enqueue([&] { load_models_and_textures(); }));
    futures.push_back(pool.enqueue([&] { load_effects(); }));
    futures.push_back(pool.enqueue([&] { spawn_staves(); }));
    futures.push_back(pool.enqueue([&] { 
        std::vector<std::filesystem::path> shield_path{ SHIELD_TEXTURE_PATH };
        renderer->load_texture_array("shield", shield_path); 
    }));
    futures.push_back(pool.enqueue([&] { renderer->load_texture_array("lightning", LIGHTNING_TEXTURE_PATHS); }));
    futures.push_back(pool.enqueue([&] { renderer->load_texture_array("fireball", FIREBALL_TEXTURE_PATHS); }));
    Log::get_instance()->add_log("[Scene] Queued asset loading jobs.\n");

    // 2. Enqueue de 10 startgebieden direct als LOSSE taken op de main threadpool!
    Log::get_instance()->add_log("Spawning characters and calculating routes...\n");
    for (size_t s = 0; s < 10; ++s) {
        futures.push_back(pool.enqueue([this, s] {
            spawn_hero_area(s);
        }));
    }

    // 3. Wacht tot ALLES (textures, staves én alle 10 hero gebieden) klaar is
    for (auto& f : futures) {
        f.get();
    }

    // 4. Vul pas NU de ruimtelijke grid, aangezien hero_system nu pas 100% zeker vol is
    const size_t hero_count = hero_system.size();
    const float* pos_x_ptr = hero_system.pos_x.data();
    const float* pos_z_ptr = hero_system.pos_z.data();
    for (size_t i = 0; i < hero_count; i++) {
        hero_grid.add_hero(static_cast<int>(i), glm::vec2(pos_x_ptr[i], pos_z_ptr[i]));
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    float total_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(total_end - total_start).count();
    
    Log::get_instance()->add_log("\n=== Scene Loading Times ===\n");
    Log::get_instance()->add_log("Terrain loading: %f ms\n", terrain_duration);
    Log::get_instance()->add_log("Total scene load (parallel): %f ms\n", total_duration);
    Log::get_instance()->add_log("[Scene] Final hero count: %zu, staff count: %zu.\n", hero_system.size(), staves.size());
    Log::get_instance()->add_log("============================\n");
}

void Scene::load_models_and_textures() const
{
    Log::get_instance()->add_log("[Scene] Loading models and textures.\n");
    renderer->load_model("frieren-blob", FRIEREN_PATH);
    renderer->load_model("frieren-lod1", FRIEREN_PATH_LOD1);
    renderer->load_model("frieren-lod2", FRIEREN_PATH_LOD2);
    renderer->load_model("frieren-lod3", FRIEREN_PATH_LOD3);
    renderer->load_texture("frieren-blob", FRIEREN_TEXTURE_PATH);

    renderer->load_model("staff", STAFF_PATH);
    renderer->load_texture("staff", STAFF_TEXTURE_PATH);
    renderer->load_model("cube", CUBE_MODEL_PATH);
    Log::get_instance()->add_log("[Scene] Models and textures loaded.\n");
}

void Scene::load_effects() const
{
    Log::get_instance()->add_log("[Scene] Loading effect texture arrays.\n");
    std::vector<std::filesystem::path> texture_paths{
        CUBE_SEA_TEXTURE_PATH,
        CUBE_GRASS_FLOWER_TEXTURE_PATH,
        CUBE_CONCRETE_WALL_TEXTURE_PATH,
        CUBE_MOSS_TEXTURE_PATH };
    renderer->load_texture_array("texture_array_test", texture_paths);
    Log::get_instance()->add_log("[Scene] Effect texture arrays loaded.\n");
}

void Scene::spawn_hero_area(size_t s)
{
    Log::get_instance()->add_log("[Scene] Spawning hero area %zu.\n", s);
    const float start_area_tile_offset = 12.f;
    const float spawn_start_y = terrain->tile_width * 3.f;
    const float start_corner_y = 9.f * terrain->tile_width;
    const float spawn_offset = terrain->tile_width / 3.f;
    const float route_cache_resolution = terrain->tile_width * 6.f; 

    const glm::uvec2 target = { 69 * terrain->tile_width, 160 * terrain->tile_width };

    HeroSystem local_heroes; 
    size_t route_cache_hits = 0;
    size_t route_cache_misses = 0;
    
    float start_area_offset = static_cast<float>(s) * start_area_tile_offset * terrain->tile_width;
    float base_x = start_corner_y + start_area_offset;

    for (int i = 0; i < 30; i++) {
        float x = base_x + (i * spawn_offset);
        for (int j = 0; j < 30; j++) {
            float z = spawn_start_y + (j * spawn_offset);
            glm::vec2 start_pos = glm::vec2(x, z);
            
            float y = terrain->get_height_fast(start_pos);
            glm::ivec2 grid_pos = glm::ivec2(start_pos / route_cache_resolution);
            
            const std::vector<glm::vec2>* shared_route_ptr = nullptr;
            bool needs_calculation = false;

            // STAP 1: Korte lock om alleen te kijken of de route al bestaat
            {
                std::lock_guard<std::mutex> lock(route_cache_mutex);
                auto route_it = global_route_cache.find(grid_pos);
                if (route_it != global_route_cache.end()) {
                    shared_route_ptr = &route_it->second;
                    route_cache_hits++;
                } else {
                    needs_calculation = true;
                }
            }

            // STAP 2: Bepaal de route PARALLEL zonder lock als hij er nog niet was
            if (needs_calculation) {
                route_cache_misses++;
                auto new_route = terrain->find_route(start_pos, target);

                // STAP 3: Korte lock om het resultaat op te slaan
                std::lock_guard<std::mutex> lock(route_cache_mutex);
                
                auto route_it = global_route_cache.find(grid_pos);
                if (route_it != global_route_cache.end()) {
                    shared_route_ptr = &route_it->second;
                } else {
                    auto inserted = global_route_cache.insert({grid_pos, algo_utils::move(new_route)});
                    shared_route_ptr = &inserted.first->second;
                }
            }

            local_heroes.add_hero(glm::vec3(x, y, z), 20.f, 0.5f, shared_route_ptr);
        }
    }

    // Voeg de 900 heroes van dit gebied veilig toe aan het centrale systeem
    std::lock_guard<std::mutex> lock(hero_mutex);
    hero_system.merge(local_heroes); 

    Log::get_instance()->add_log(
        "[Scene] Hero area %zu ready: %zu heroes, %zu route cache hits, %zu route cache misses.\n",
        s,
        local_heroes.size(),
        route_cache_hits,
        route_cache_misses
    );
}

void Scene::spawn_staves()
{
    Log::get_instance()->add_log("[Scene] Spawning staves.\n");
    glm::vec2 spawn_start{ terrain->tile_width * 15.f, terrain->tile_length * 48.f };
    float height = terrain->get_height_fast(spawn_start) + 50.f;
    
    float spawn_offset_x = 12.f * terrain->tile_height;
    float spawn_offset_y = 40.f * terrain->tile_length;

    float spawn_count = 0;
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            spawn_count++;
            glm::vec3 position{ spawn_start.x + i * spawn_offset_x, height, spawn_start.y + j * spawn_offset_y };
            staves.emplace_back(position, terrain.get());
        }
    }

    Log::get_instance()->add_log("Spawned %d staves.\n", static_cast<int>(spawn_count));
}

size_t Scene::get_character_count() const { return hero_system.size(); }
size_t Scene::get_staff_count() const { return staves.size(); }

void Scene::check_collisions()
{
    // Raw pointers uit SoA structuren voor maximale L1 cache hitting & vectorisatie
    const float* pos_x_ptr = hero_system.pos_x.data();
    const float* pos_z_ptr = hero_system.pos_z.data();
    const float* rad_ptr = hero_system.collision_radius.data();
    const uint8_t* active_ptr = hero_system.active.data();
    glm::vec2* force_ptr = hero_system.force.data();

    // Cache de interne array pointers van de grid
    const int* grid_next_ptr = hero_grid.next.data();

    pool.parallel_for_chunked(hero_system.size(), [&](size_t i, size_t chunk_id) {
        if (!active_ptr[i]) return;

        const glm::vec2 pos_i(pos_x_ptr[i], pos_z_ptr[i]);
        const float radius_i = rad_ptr[i];
        const int cell = hero_grid.get_cell_id(pos_i);
        
        glm::vec2 force{ 0.f };
        int j = hero_grid.get_head(cell); 
        
        while (j != -1) {
            if (i != static_cast<size_t>(j)) { 
                const float diff_x = pos_x_ptr[j] - pos_i.x;
                const float diff_y = pos_z_ptr[j] - pos_i.y;
                
                const float dist_sq = (diff_x * diff_x) + (diff_y * diff_y);
                const float radius_sum = radius_i + rad_ptr[j];
                const float radius_sum_sq = radius_sum * radius_sum;
                
                if (dist_sq < radius_sum_sq && dist_sq > 0.0001f) {
                    const float inv_dist = math_utils::fast_inv_sqrt(dist_sq);
                    const float multiplier = (radius_sum * inv_dist) - 1.0f; 
                    
                    force.x -= diff_x * multiplier;
                    force.y -= diff_y * multiplier; 
                }
            }
            j = grid_next_ptr[j]; 
        }
        
        force_ptr[i] += force; 
    });
}

void Scene::update(const float delta_time)
{
    const uint32_t phase = update_frame++ & 3u;
    handle_input(delta_time);
    
    if (follow_mode && !hero_system.empty()) {
        size_t max_idx = 0;
        float max_z = math_utils::FLT_LOWEST_VAL;
        
        const size_t total_heroes = hero_system.size();
        const uint8_t* active_ptr = hero_system.active.data();
        const float* pos_z_ptr = hero_system.pos_z.data();

        for (size_t i = 0; i < total_heroes; ++i) {
            if (active_ptr[i] && pos_z_ptr[i] > max_z) {
                max_z = pos_z_ptr[i];
                max_idx = i;
            }
        }
        
        static float last_z = math_utils::FLT_LOWEST_VAL;
        const glm::vec3 hero_pos(hero_system.pos_x[max_idx], hero_system.pos_y[max_idx], hero_system.pos_z[max_idx]);
        
        if (math_utils::abs(hero_pos.z - last_z) > 0.001f) {
            last_z = hero_pos.z;
            const glm::vec3 camera_pos{ -28.0f, 305.5f, hero_pos.z };
            camera.set_position(camera_pos);
            camera.set_direction(hero_pos - camera_pos);
        }
    }

    renderer->set_view_matrix(camera.get_view_matrix());
    
    if (phase == 0) {
        hero_grid.clear();
        hero_system.flush_mutations();
        check_collisions();
    }
    
    size_t total_heroes = hero_system.size();
    
    
    pool.parallel_for_blocks(hero_system.size(), 256, [&](size_t start, size_t end) {
    hero_system.update_heroes_batch(start, end, delta_time, *terrain, phase);
    });
    
    if (phase == 1)
    {
        pool.parallel_for_chunked(projectiles.size(), [&](size_t i, size_t chunk_id) {
            projectiles[i].update(delta_time * 4.0f, camera, shield, hero_system);
        });    
    }

    if (phase == 2)
    {
        shield.update(hero_system, &pool);
    }
    
    if (phase == 3)
    {
        pool.parallel_for_chunked(active_lightning.size(), [&](size_t i, size_t chunk_id) {
            active_lightning[i].update(delta_time * 4.0f, camera, hero_system);
        });
        
        pool.parallel_for_chunked(staves.size(), [&](size_t i, size_t chunk_id) {
            staves[i].update(delta_time * 4.0f, hero_system, active_lightning, projectiles);
        });
    }
    
    hero_system.flush_mutations();

    auto cleanup = [](auto& container) {
        size_t i = 0;
        while (i < container.size()) {
            if (!container[i].is_active()) {
                container[i] = algo_utils::move(container.back());
                container.pop_back();
            } else {
                ++i;
            }
        }
    };

    if (!projectiles.empty()) cleanup(projectiles);
    if (!active_lightning.empty()) cleanup(active_lightning);
}

void Scene::draw()
{
    visible_hero_count = 0;
    const glm::vec3 cam_pos = camera.get_position();
    
    const size_t hero_count  = hero_system.size();
    const size_t staff_count = staves.size();

    const size_t num_workers = pool.thread_count();
    const size_t target_chunks = num_workers * 8;

    if (lod0_chunks.size() < target_chunks) {
        lod0_chunks.resize(target_chunks);
        lod1_chunks.resize(target_chunks);
        lod2_chunks.resize(target_chunks);
        staff_chunks.resize(target_chunks);
    }

    for (size_t c = 0; c < target_chunks; c++) {
        lod0_chunks[c].count = 0;
        lod1_chunks[c].count = 0;
        lod2_chunks[c].count = 0;
        staff_chunks[c].count = 0;
    }

    const float* pos_x_ptr = hero_system.pos_x.data();
    const float* pos_z_ptr = hero_system.pos_z.data();

    pool.parallel_for_chunked(hero_count, [&](size_t i, size_t chunk_id) {
        const float dx = pos_x_ptr[i] - cam_pos.x;
        const float dz = pos_z_ptr[i] - cam_pos.z;
        const float dist2 = (dx * dx) + (dz * dz);

        if (dist2 < LOD2_DIST2) {
            glm::mat4 transform = hero_system.get_transform_matrix(i);
            
            if (dist2 < LOD0_DIST2)      lod0_chunks[chunk_id].data[lod0_chunks[chunk_id].count++] = transform;
            else if (dist2 < LOD1_DIST2) lod1_chunks[chunk_id].data[lod1_chunks[chunk_id].count++] = transform;
            else                         lod2_chunks[chunk_id].data[lod2_chunks[chunk_id].count++] = transform;
        }
    });

    pool.parallel_for_chunked(staff_count, [&](size_t i, size_t chunk_id) {
        glm::mat4 transform = staves[i].get_transform_matrix();
        const glm::vec3 pos = glm::vec3(transform[3]); 
        
        const float dx = pos.x - cam_pos.x;
        const float dz = pos.z - cam_pos.z;
        const float dist2 = (dx * dx) + (dz * dz);

        if (dist2 < LOD2_DIST2) {
            staff_chunks[chunk_id].data[staff_chunks[chunk_id].count++] = transform;
        }
    });

    size_t total_lod0 = 0, total_lod1 = 0, total_lod2 = 0, total_staff = 0;
    for (size_t c = 0; c < target_chunks; c++) {
        total_lod0 += lod0_chunks[c].count;
        total_lod1 += lod1_chunks[c].count;
        total_lod2 += lod2_chunks[c].count;
        total_staff += staff_chunks[c].count;
    }

    lod0.resize(total_lod0);
    lod1.resize(total_lod1);
    lod2.resize(total_lod2);
    staff_transforms.resize(total_staff);
    
    size_t offset0 = 0, offset1 = 0, offset2 = 0, offset_staff = 0;
    for (size_t c = 0; c < target_chunks; c++) {
        if (lod0_chunks[c].count > 0) {
            algo_utils::fast_copy_mat4(&lod0[offset0], lod0_chunks[c].data.data(), lod0_chunks[c].count);
            offset0 += lod0_chunks[c].count;
        }
        if (lod1_chunks[c].count > 0) {
            algo_utils::fast_copy_mat4(&lod1[offset1], lod1_chunks[c].data.data(), lod1_chunks[c].count);
            offset1 += lod1_chunks[c].count;
        }
        if (lod2_chunks[c].count > 0) {
            algo_utils::fast_copy_mat4(&lod2[offset2], lod2_chunks[c].data.data(), lod2_chunks[c].count);
            offset2 += lod2_chunks[c].count;
        }
        if (staff_chunks[c].count > 0) {
            algo_utils::fast_copy_mat4(&staff_transforms[offset_staff], staff_chunks[c].data.data(), staff_chunks[c].count);
            offset_staff += staff_chunks[c].count;
        }
    }
    
    if (!lod0.empty()) renderer->draw_batch("frieren-blob", "frieren-blob", lod0);
    if (!lod1.empty()) renderer->draw_batch("frieren-lod1", "frieren-blob", lod1);
    if (!lod2.empty()) renderer->draw_batch("frieren-lod2", "frieren-blob", lod2);
    
    if (!staff_transforms.empty()) renderer->draw_batch("staff", "staff", staff_transforms);
        
    glm::mat4 view_proj = camera.get_projection_matrix() * camera.get_view_matrix();
    terrain->draw(renderer, cam_pos, view_proj);
        
    for (const auto& lightning : active_lightning) { lightning.register_draw(lightning_sprite_manager); }
    lightning_sprite_manager.draw(renderer);
    lightning_sprite_manager.reset();

    for (const auto& projectile : projectiles) { projectile.register_draw(projectile_sprite_manager); }
    projectile_sprite_manager.draw(renderer);
    projectile_sprite_manager.reset();

    shield.draw(renderer);

    if (show_debug_windows) {
        show_health_values();
        show_mana_values();
        Log::get_instance()->draw("Log");
        show_controls();
    }
}
    
void Scene::show_health_values() const
{
    // Static vector voorkomt allocatie-overhead op elke frame wanneer ImGui actief is
    static std::vector<int> health_values;
    health_values.clear();
    health_values.reserve(hero_system.size());

    const uint8_t* active_ptr = hero_system.active.data();
    const int* health_ptr = hero_system.health.data();
    const size_t total = hero_system.size();

    for (size_t i = 0; i < total; i++) {
        if (active_ptr[i]) {
            health_values.push_back(health_ptr[i]);
        }
    }

    algo_utils::parallel_sort(health_values);

    ImGui::Begin("Heroes Health Bars");
    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.90f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.f, 0.5f, 0.f, 1.0f }); 

    for (const int& hp : health_values) {
        char hp_text[16];
        snprintf(hp_text, sizeof(hp_text), "%d/1000", hp);
        ImGui::ProgressBar((float)hp / 1000.0f, ImVec2(-FLT_MIN, 0.0f), hp_text);
    }
    
    ImGui::PopStyleColor(1);
    ImGui::End();
}

void Scene::show_mana_values() const
{
    static std::vector<int> mana_values;
    mana_values.clear();
    mana_values.reserve(hero_system.size()); 

    const uint8_t* active_ptr = hero_system.active.data();
    const int* mana_ptr = hero_system.mana.data();
    const size_t total = hero_system.size();

    for (size_t i = 0; i < total; i++) {
        if (active_ptr[i]) {
            mana_values.push_back(mana_ptr[i]);
        }
    }

    algo_utils::parallel_sort(mana_values);

    ImGui::Begin("Heroes Mana Bars");
    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.90f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.f, 0.f, 0.5f, 1.0f }); 
    
    for (const int& mana : mana_values)
    {
        char mana_text[16];
        snprintf(mana_text, sizeof(mana_text), "%d/1000", mana);
        ImGui::ProgressBar((float)mana / 1000.0f, ImVec2(-FLT_MIN, 0.0f), mana_text);
    }
    
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    ImGui::End();
}

void Scene::handle_input(const float delta_time)
{
    const bool f1_pressed = glfwGetKey(renderer->get_window(), GLFW_KEY_F1) == GLFW_PRESS;
    if (f1_pressed && !f1_was_pressed) {
        show_debug_windows = !show_debug_windows;
        Log::get_instance()->add_log("[Scene/Input] Debug windows %s.\n", show_debug_windows ? "enabled" : "disabled");
    }
    f1_was_pressed = f1_pressed;

    const bool tab_pressed = glfwGetKey(renderer->get_window(), GLFW_KEY_TAB) == GLFW_PRESS;
    if (tab_pressed && !tab_was_pressed) {
        follow_mode = !follow_mode;
        Log::get_instance()->add_log("[Scene/Input] Follow mode %s.\n", follow_mode ? "enabled" : "disabled");
    }
    tab_was_pressed = tab_pressed;

    if (!follow_mode)
    {
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_W) == GLFW_PRESS) { camera.move_forward(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_S) == GLFW_PRESS) { camera.move_backward(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_Q) == GLFW_PRESS) { camera.move_left(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_E) == GLFW_PRESS) { camera.move_right(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_A) == GLFW_PRESS) { camera.rotate_left(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_D) == GLFW_PRESS) { camera.rotate_right(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_SPACE) == GLFW_PRESS) { camera.move_up(delta_time); }
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_Z) == GLFW_PRESS) { camera.move_down(delta_time); }

        glm::dvec2 mouse_pos;
        glfwGetCursorPos(renderer->get_window(), &mouse_pos.x, &mouse_pos.y);

        glm::dvec2 mouse_offset = mouse_pos - prev_mouse_pos;
        prev_mouse_pos = mouse_pos;

        mouse_offset.x *= delta_time;
        mouse_offset.y *= delta_time;

        if (glfwGetKey(renderer->get_window(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        {
            camera.update_direction(mouse_offset);
        }
    }
}

void Scene::show_controls()
{
    ImGui::Begin("Camera Controls Guide");

    ImGui::Text("Follow Mode: %s", follow_mode ? "Enabled" : "Disabled");
    ImGui::Separator();

    ImGui::Text("Movement Controls (When Follow Mode is Disabled):");
    ImGui::BulletText("[W] - Move Forward");
    ImGui::BulletText("[S] - Move Backward");
    ImGui::BulletText("[Q] - Move Left");
    ImGui::BulletText("[E] - Move Right");
    ImGui::BulletText("[A] - Rotate Left");
    ImGui::BulletText("[D] - Rotate Right");
    ImGui::BulletText("[SPACE] - Move Up");
    ImGui::BulletText("[Z] - Move Down");

    ImGui::Separator();

    ImGui::Text("Mouse Controls:");
    ImGui::BulletText("[Mouse + SHIFT] - Look Around");

    ImGui::Separator();

    ImGui::Text("Current Mouse Position:");
    ImGui::Text("X: %.2f, Y: %.2f", prev_mouse_pos.x, prev_mouse_pos.y);

    ImGui::Text("Camera Position:");
    glm::vec3 camera_pos = camera.get_position();
    ImGui::Text("X: %.2f, Y: %.2f, Z: %.2f", camera_pos.x, camera_pos.y, camera_pos.z);

    ImGui::Text("Camera Direction:");
    glm::vec3 camera_dir = camera.get_direction();
    ImGui::Text("X: %.2f, Y: %.2f, Z: %.2f", camera_dir.x, camera_dir.y, camera_dir.z);

    ImGui::Separator();
    ImGui::Text("Toggle Follow Mode: [TAB]");
    ImGui::End();
}
