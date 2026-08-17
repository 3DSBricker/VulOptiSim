#include "pch.h"
#include "scene.h"
#include <numeric>
#include <execution>
#include <algorithm>
#include <immintrin.h> // VERPLICHT voor _mm_rsqrt_ss

Scene::Scene(vulvox::Renderer* renderer) : renderer(renderer), terrain(std::make_unique<Terrain>(TERRAIN_PATH)),
pool(std::max(1u, std::thread::hardware_concurrency())),
hero_grid(10000.0f, 8.0f) 
{
    auto total_start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<void>> future;
    glfwGetCursorPos(this->renderer->get_window(), &prev_mouse_pos.x, &prev_mouse_pos.y);

    glm::vec3 camera_pos{ -28.2815380f, 305.485260f, -30.0800228f };
    glm::vec3 camera_up{ 0.338442326f, 0.869414926f, 0.359964609f };
    glm::vec3 camera_direction{ 0.595541596f, -0.494082689f, 0.633413374f };

    camera = Camera(camera_pos, camera_up, camera_direction, 100.f, 100.f);

    auto terrain_start = std::chrono::high_resolution_clock::now();
    // terrain = Terrain(TERRAIN_PATH);
    terrain->initialize(renderer);
    std::cout << "Terrain ptr: " << terrain.get() << '\n';
    auto terrain_end = std::chrono::high_resolution_clock::now();
    float terrain_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(terrain_end - terrain_start).count();

    shield = Shield{ "shield" };

    future.push_back(pool.enqueue([this] {
        auto models_start = std::chrono::high_resolution_clock::now();
        load_models_and_textures();
        auto models_end = std::chrono::high_resolution_clock::now();
        float models_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(models_end - models_start).count();
        std::cout << "Models & textures loading took: " << models_duration << " ms" << std::endl;
    }));

    future.push_back(pool.enqueue([this] {
        auto effects_start = std::chrono::high_resolution_clock::now();
        load_effects();
        auto effects_end = std::chrono::high_resolution_clock::now();
        float effects_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(effects_end - effects_start).count();
        std::cout << "Essential effects loading took: " << effects_duration << " ms" << std::endl;
        }));

    future.push_back(pool.enqueue([this] {
        auto spawn_start = std::chrono::high_resolution_clock::now();
        spawn_heroes();
        auto spawn_end = std::chrono::high_resolution_clock::now();
        float spawn_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(spawn_end - spawn_start).count();
        std::cout << "Spawn loading took: " << spawn_duration << " ms" << std::endl;
    }));

    future.push_back(pool.enqueue([this] {
        auto staves_start = std::chrono::high_resolution_clock::now();
        spawn_staves();
        auto staves_end = std::chrono::high_resolution_clock::now();
        float staves_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(staves_end - staves_start).count();
        std::cout << "Staves loading took: " << staves_duration << " ms" << std::endl;
        }));

    for (auto& f : future) {
        f.get();
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    float total_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(total_end - total_start).count();

    std::cout << "\n=== Scene Loading Times ===\n"
              << "Terrain loading: " << terrain_duration << " ms\n"
              << "Total scene load (parallel): " << total_duration << " ms\n"
              << "============================\n" << std::endl;

    std::cout << "\n>>> Background: Loading animation effects (parallel)...\n" << std::endl;
    // pool.enqueue([this] {
        // auto anim_start = std::chrono::high_resolution_clock::now();
        load_animation_effects();
        // auto anim_end = std::chrono::high_resolution_clock::now();
        // float anim_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(anim_end - anim_start).count();
        // std::cout << "Animation effects loading took: " << anim_duration << " ms (done in background)\n" << std::endl;
    // });
}

void Scene::load_models_and_textures() const
{
    renderer->load_model("frieren-blob", FRIEREN_PATH);
    renderer->load_model("frieren-lod1", FRIEREN_PATH_LOD1);
    renderer->load_model("frieren-lod2", FRIEREN_PATH_LOD2);
    renderer->load_model("frieren-lod3", FRIEREN_PATH_LOD3);
    renderer->load_texture("frieren-blob", FRIEREN_TEXTURE_PATH);

    renderer->load_model("staff", STAFF_PATH);
    renderer->load_texture("staff", STAFF_TEXTURE_PATH);
    renderer->load_model("cube", CUBE_MODEL_PATH);
}

void Scene::load_effects() const
{
    std::vector<std::filesystem::path> texture_paths{
        CUBE_SEA_TEXTURE_PATH,
        CUBE_GRASS_FLOWER_TEXTURE_PATH,
        CUBE_CONCRETE_WALL_TEXTURE_PATH,
        CUBE_MOSS_TEXTURE_PATH };
    renderer->load_texture_array("texture_array_test", texture_paths);
}

void Scene::load_animation_effects() const
{
    std::vector<std::future<void>> futures;
    
    futures.push_back(std::async(std::launch::async, [this] {
        std::vector<std::filesystem::path> shield_path{ SHIELD_TEXTURE_PATH };
        renderer->load_texture_array("shield", shield_path);
    }));
    
    futures.push_back(std::async(std::launch::async, [this] {
        renderer->load_texture_array("lightning", LIGHTNING_TEXTURE_PATHS);
    }));
    
    futures.push_back(std::async(std::launch::async, [this] {
        renderer->load_texture_array("fireball", FIREBALL_TEXTURE_PATHS);
    }));
    
    for (auto& f : futures) {
        f.get();
    }
}

void Scene::spawn_heroes()
{
    std::cout << "Terrain ptr: " << terrain.get() << '\n';

    int start_areas = 10;
    float start_area_tile_offset = 12.f;
    float spawn_start_y = terrain->tile_width * 3.f;

    float start_corner_y = 9.f * terrain->tile_width;
    float spawn_offset = terrain->tile_width / 3.f;
    float route_cache_resolution = terrain->tile_width * 6.f; 

    std::cout << "Spawning characters and calculating routes..." << std::endl;

    std::vector<std::future<void>> futures;
    glm::uvec2 target = { 69 * terrain->tile_width, 160 * terrain->tile_width };
    
    for (int s = 0; s < start_areas; s++)
    {
        futures.push_back(pool.enqueue([this, s, spawn_offset, start_corner_y, spawn_start_y, start_area_tile_offset, target, route_cache_resolution] {
            HeroSystem local_heroes; 
            
            float start_area_offset = s * start_area_tile_offset * terrain->tile_width;
            float base_x = start_corner_y + start_area_offset;

            for (int i = 0; i < 30; i++) {
                float x = base_x + (i * spawn_offset);
                for (int j = 0; j < 30; j++) {
                    float z = spawn_start_y + (j * spawn_offset);
                    glm::vec2 start_pos = glm::vec2(x, z);
                    
                    // Optimalisatie: Direct get_height_fast aanroepen (sneller dan standaard get_height)
                    float y = terrain->get_height_fast(start_pos);
                    
                    glm::ivec2 grid_pos = glm::ivec2(start_pos / route_cache_resolution);
                    
                    const std::vector<glm::vec2>* shared_route_ptr = nullptr;
                    {
                        std::lock_guard<std::mutex> lock(route_cache_mutex);
                        auto route_it = global_route_cache.find(grid_pos);
                        if (route_it != global_route_cache.end()) {
                            shared_route_ptr = &route_it->second;
                        } else {
                            // Insert direct in map en haal pointer naar de stabiele data op
                            auto inserted = global_route_cache.insert({grid_pos, terrain->find_route(start_pos, target)});
                            shared_route_ptr = &inserted.first->second;
                        }
                    }
                    local_heroes.add_hero(glm::vec3(x, y, z), 20.f, 0.5f, shared_route_ptr);
                }
            }

            std::lock_guard<std::mutex> lock(hero_mutex);
            hero_system.merge(local_heroes); 
        }));
    }

    for (auto& f : futures) f.get();

    for(size_t i = 0; i < hero_system.size(); i++) {
        hero_grid.add_hero(i, glm::vec2(hero_system.position[i].x, hero_system.position[i].z));
    }
}

void Scene::spawn_staves()
{
    glm::vec2 spawn_start{ terrain->tile_width * 15.f, terrain->tile_length * 48.f };
    float height = terrain->get_height_fast(spawn_start) + 50.f; // Ook hier snellere call
    
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
    // Trek pointers los uit de Struct of Arrays (SoA)
    const glm::vec3* pos_ptr = hero_system.position.data();
    const float* rad_ptr = hero_system.collision_radius.data();
    const uint8_t* active_ptr = hero_system.active.data();
    glm::vec2* force_ptr = hero_system.force.data();

    pool.parallel_for_chunked(hero_system.size(), [&](size_t i, size_t chunk_id) {
        if (!active_ptr[i]) return;

        const glm::vec2 pos_i(pos_ptr[i].x, pos_ptr[i].z);
        const float radius_i = rad_ptr[i];
        const int cell = hero_grid.get_cell_id(pos_i);
        
        glm::vec2 force{ 0.f };
        int j = hero_grid.get_head(cell); 
        
        while (j != -1) {
            if (i != (size_t)j) { 
                const float diff_x = pos_ptr[j].x - pos_i.x;
                const float diff_y = pos_ptr[j].z - pos_i.y; 
                
                const float dist_sq = (diff_x * diff_x) + (diff_y * diff_y);
                const float radius_sum = radius_i + rad_ptr[j];
                const float radius_sum_sq = radius_sum * radius_sum;
                
                if (dist_sq < radius_sum_sq && dist_sq > 0.0001f) {
                // Hardware matige fast inverse square root (approx 4 CPU cycles!)
                float inv_dist = _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(dist_sq)));
                
                // Optioneel: 1 Newton-Raphson iteratie als het stottert, maar voor collisions is de schatting accuraat genoeg
                // inv_dist = inv_dist * (1.5f - (0.5f * dist_sq * inv_dist * inv_dist));

                // Wiskundig gereduceerde overlap-deling
                float multiplier = (radius_sum * inv_dist) - 1.0f; 
                
                force.x -= diff_x * multiplier;
                force.y -= diff_y * multiplier; 
                }
            }
            j = hero_grid.next[j]; 
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
        float max_z = std::numeric_limits<float>::lowest();
        
        for (size_t i = 0; i < hero_system.size(); ++i) {
            if (hero_system.active[i] && hero_system.position[i].z > max_z) {
                max_z = hero_system.position[i].z;
                max_idx = i;
            }
        }
        
        static float last_z = std::numeric_limits<float>::lowest();
        const glm::vec3 hero_pos = hero_system.position[max_idx];
        
        if (std::abs(hero_pos.z - last_z) > 0.001f) {
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
    
    // FIX: Chunked parallel loop lost False Sharing over SoA data compleet op!
    pool.parallel_for_chunked(hero_system.size(), [&](size_t i, size_t chunk_id) {
        hero_system.update_hero(i, delta_time, *terrain, phase);
    });
    
    
    if(phase == 1)
    {
        // FIX: Projectiles profiteerden niet van aaneengesloten data, nu wel.
        pool.parallel_for_chunked(projectiles.size(), [&](size_t i, size_t chunk_id) {
            projectiles[i].update(delta_time * 4.0f, camera, shield, hero_system);
        });    
    }

    if(phase == 2)
    {
        // Let op: Dit slokt nog 19% van de pie chart op, omdat het blokkeert op de main thread. 
        // Als je de raw code hiervan (en van Lightning/Staves) deelt, verhelpen we die ook.
        shield.update(hero_system, &pool);
    }
    
    if(phase == 3)
    {
        // Haal Lightning van de main thread af
        pool.parallel_for_chunked(active_lightning.size(), [&](size_t i, size_t chunk_id) {
            active_lightning[i].update(delta_time * 4.0f, camera, hero_system);
        });
        
        // Haal Staves van de main thread af
        pool.parallel_for_chunked(staves.size(), [&](size_t i, size_t chunk_id) {
            staves[i].update(delta_time * 4.0f, hero_system, active_lightning, projectiles);
        });
    }
    
    hero_system.flush_mutations();

    auto cleanup = [](auto& container) {
        size_t i = 0;
        while (i < container.size()) {
            if (!container[i].is_active()) {
                container[i] = std::move(container.back());
                container.pop_back();
            } else {
                ++i;
            }
        }
    };

    if(!projectiles.empty()) cleanup(projectiles);
    if(!active_lightning.empty()) cleanup(active_lightning);
}

void Scene::draw()
{
    visible_hero_count = 0;
    const glm::vec3 cam_pos = camera.get_position();
    
    const size_t hero_count  = hero_system.size();
    const size_t staff_count = staves.size();
    const size_t num_workers = pool.thread_count();
    
    if (lod0_chunks.size() != num_workers) {
        lod0_chunks.resize(num_workers);
        lod1_chunks.resize(num_workers);
        lod2_chunks.resize(num_workers);
    }

    if (staff_chunks.size() != num_workers) {
        staff_chunks.resize(num_workers);
    }
    
    pool.parallel_for(num_workers, [&](size_t w) {
        lod0_chunks[w].count = 0;
        lod1_chunks[w].count = 0;
        lod2_chunks[w].count = 0;
        staff_chunks[w].count = 0;
    });

    pool.parallel_for_chunked(hero_count, [&](size_t i, size_t chunk_id) {
        glm::vec3 pos = hero_system.position[i];
        
        float dx = pos.x - cam_pos.x;
        float dz = pos.z - cam_pos.z;
        float dist2 = (dx * dx) + (dz * dz);

        if (dist2 < LOD2_DIST2) {
            glm::mat4 transform = hero_system.get_transform_matrix(i);
            
            if (dist2 < LOD0_DIST2)      lod0_chunks[chunk_id].data[lod0_chunks[chunk_id].count++] = transform;
            else if (dist2 < LOD1_DIST2) lod1_chunks[chunk_id].data[lod1_chunks[chunk_id].count++] = transform;
            else                         lod2_chunks[chunk_id].data[lod2_chunks[chunk_id].count++] = transform;
        }
    });

    pool.parallel_for_chunked(staff_count, [&](size_t i, size_t chunk_id) {
        glm::mat4 transform = staves[i].get_transform_matrix();
        glm::vec3 pos = glm::vec3(transform[3]); 
        
        float dx = pos.x - cam_pos.x;
        float dz = pos.z - cam_pos.z;
        float dist2 = (dx * dx) + (dz * dz);

        if (dist2 < LOD2_DIST2) {
            staff_chunks[chunk_id].data[staff_chunks[chunk_id].count++] = transform;
        }
    });

    // 1. Bereken eerst de totale groottes
    size_t total_lod0 = 0, total_lod1 = 0, total_lod2 = 0, total_staff = 0;
    for (size_t w = 0; w < num_workers; w++) {
        total_lod0 += lod0_chunks[w].count;
        total_lod1 += lod1_chunks[w].count;
        total_lod2 += lod2_chunks[w].count;
        total_staff += staff_chunks[w].count;
    }
    // 2. Resize eenmalig. Zonder voorafgaande clear() krimpt de vector gratis,
    // en bij groei initialiseert hij alléén het verschil.
    lod0.resize(total_lod0);
    lod1.resize(total_lod1);
    lod2.resize(total_lod2);
    staff_transforms.resize(total_staff);
    
    // 3. Kopieer de data direct naar de juiste offset
    size_t offset0 = 0, offset1 = 0, offset2 = 0, offset_staff = 0;
    for (size_t w = 0; w < num_workers; w++) {
        if(lod0_chunks[w].count > 0) {
            std::memcpy(&lod0[offset0], lod0_chunks[w].data.data(), lod0_chunks[w].count * sizeof(glm::mat4));
            offset0 += lod0_chunks[w].count;
        }
        if(lod1_chunks[w].count > 0) {
            std::memcpy(&lod1[offset1], lod1_chunks[w].data.data(), lod1_chunks[w].count * sizeof(glm::mat4));
            offset1 += lod1_chunks[w].count;
        }
        if(lod2_chunks[w].count > 0) {
            std::memcpy(&lod2[offset2], lod2_chunks[w].data.data(), lod2_chunks[w].count * sizeof(glm::mat4));
            offset2 += lod2_chunks[w].count;
        }
        if(staff_chunks[w].count > 0) {
            std::memcpy(&staff_transforms[offset_staff], staff_chunks[w].data.data(), staff_chunks[w].count * sizeof(glm::mat4));
            offset_staff += staff_chunks[w].count;
        }
    }
    
    if (!lod0.empty()) renderer->draw_batch("frieren-blob", "frieren-blob", lod0);
    if (!lod1.empty()) renderer->draw_batch("frieren-lod1", "frieren-blob", lod1);
    if (!lod2.empty()) renderer->draw_batch("frieren-lod2", "frieren-blob", lod2);
    
    if (!staff_transforms.empty()) renderer->draw_batch("staff", "staff", staff_transforms);
        
    terrain->draw(renderer, cam_pos);
        
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
    std::vector<int> health_values;
    health_values.reserve(hero_system.size());

    for (size_t i = 0; i < hero_system.size(); i++) {
        if (hero_system.active[i]) {
            health_values.push_back(hero_system.health[i]);
        }
    }

    sort(health_values);

    ImGui::Begin("Heroes Health Bars");
    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.90f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.f, 0.5f, 0.f, 1.0f }); 

    for (const int& hp : health_values) {
        char hp_text[16];
        snprintf(hp_text, sizeof(hp_text), "%d/1000", hp);
        ImGui::ProgressBar((float)hp / 1000, ImVec2(-FLT_MIN, 0.0f), hp_text);
    }
    
    ImGui::PopStyleColor(1);
    ImGui::End();
}

void Scene::show_mana_values() const
{
    std::vector<int> mana_values;
    mana_values.reserve(hero_system.size()); 

    for (size_t i = 0; i < hero_system.size(); i++) {
        if (hero_system.active[i]) {
            mana_values.push_back(hero_system.mana[i]);
        }
    }

    sort(mana_values);

    ImGui::Begin("Heroes Mana Bars");
    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.90f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.f, 0.f, 0.5f, 1.0f }); 
    
    for (const int& mana : mana_values)
    {
        char mana_text[16];
        snprintf(mana_text, sizeof(mana_text), "%d/1000", mana);
        ImGui::ProgressBar((float)mana / 1000, ImVec2(-FLT_MIN, 0.0f), mana_text);
    }
    
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    ImGui::End();
}

void Scene::sort(std::vector<int>& arr) const
{
    quicksort(arr, 0, arr.size() - 1);
}

void Scene::quicksort(std::vector<int>& arr, int low, int high) const
{
    if (low >= high) return;

    int pivot = arr[(low + high) / 2];
    int left = low, right = high;

    while (left <= right)
    {
        while (arr[left] < pivot) left++;
        while (arr[right] > pivot) right--;

        if (left <= right)
        {
            std::swap(arr[left], arr[right]);
            left++;
            right--;
        }
    }

    quicksort(arr, low, right);  
    quicksort(arr, left, high);  
}

void Scene::handle_input(const float delta_time)
{
    const bool f1_pressed = glfwGetKey(renderer->get_window(), GLFW_KEY_F1) == GLFW_PRESS;
    if (f1_pressed && !f1_was_pressed) {
        show_debug_windows = !show_debug_windows;
    }
    f1_was_pressed = f1_pressed;

    if (glfwGetKey(renderer->get_window(), GLFW_KEY_TAB) == GLFW_PRESS) { follow_mode = !follow_mode; }

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