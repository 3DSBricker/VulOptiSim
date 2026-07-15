#include "pch.h"
#include "scene.h"
#include <numeric>
#include <execution>
#include <algorithm> // Nodig voor std::for_each

Scene::Scene(vulvox::Renderer& renderer) : renderer(&renderer),
pool(std::min<size_t>(4, std::max(1u, std::thread::hardware_concurrency()))),
hero_grid(8.0f) // Kleinere cells = minder heroes per cell = snellere checks
{
    auto total_start = std::chrono::high_resolution_clock::now();
    std::vector<std::future<void>> future;
    glfwGetCursorPos(this->renderer->get_window(), &prev_mouse_pos.x, &prev_mouse_pos.y);

    glm::vec3 camera_pos{ -28.2815380f, 305.485260f, -30.0800228f };
    glm::vec3 camera_up{ 0.338442326f, 0.869414926f, 0.359964609f };
    glm::vec3 camera_direction{ 0.595541596f, -0.494082689f, 0.633413374f };

    camera = Camera(camera_pos, camera_up, camera_direction, 100.f, 100.f);

    auto terrain_start = std::chrono::high_resolution_clock::now();
    terrain = Terrain(TERRAIN_PATH);
    auto terrain_end = std::chrono::high_resolution_clock::now();
    float terrain_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(terrain_end - terrain_start).count();

    shield = Shield{ "shield" };

    // Laad models & textures PARALLEL met effects/spawn/staves
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

    // wacht tot alle taken klaar zijn
    for (auto& f : future) {
        f.get();
    }

    auto total_end = std::chrono::high_resolution_clock::now();
    float total_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(total_end - total_start).count();

    std::cout << "\n=== Scene Loading Times ===\n"
              << "Terrain loading: " << terrain_duration << " ms\n"
              << "Total scene load (parallel): " << total_duration << " ms\n"
              << "============================\n" << std::endl;

    // Laad animation effects ASYNC na scene klaar (niet blocking)
    std::cout << "\n>>> Background: Loading animation effects (parallel)...\n" << std::endl;
    pool.enqueue([this] {
        auto anim_start = std::chrono::high_resolution_clock::now();
        load_animation_effects();
        auto anim_end = std::chrono::high_resolution_clock::now();
        float anim_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(anim_end - anim_start).count();
        std::cout << "Animation effects loading took: " << anim_duration << " ms (done in background)\n" << std::endl;
    });
}

void Scene::load_models_and_textures() const
{
    //Load all the models and textures we're going to need into GPU memory

    //NPCs
    //renderer->load_model("konata", MODEL_PATH);
    //renderer->load_texture("konata", KONATA_MODAL_TEXTURE_PATH);

    renderer->load_model("frieren-blob", FRIEREN_PATH);
    renderer->load_texture("frieren-blob", FRIEREN_TEXTURE_PATH);

    renderer->load_model("staff", STAFF_PATH);
    renderer->load_texture("staff", STAFF_TEXTURE_PATH);

    renderer->load_model("cube", CUBE_MODEL_PATH);
    //renderer->load_texture("cube", CUBE_SEA_TEXTURE_PATH); // onnodig want al in texutre_paths
}

void Scene::load_effects() const
{
    // ECHT essentieel: terrain textures die ALTIJD zichtbaar zijn
    std::vector<std::filesystem::path> texture_paths{
        CUBE_SEA_TEXTURE_PATH,
        CUBE_GRASS_FLOWER_TEXTURE_PATH,
        CUBE_CONCRETE_WALL_TEXTURE_PATH,
        CUBE_MOSS_TEXTURE_PATH };
    renderer->load_texture_array("texture_array_test", texture_paths);
    // Alles ander (shield, lightning, fireball) laad async
}

void Scene::load_animation_effects() const
{
    // Parallelize texture array loading: 3 threads, elke array apart
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
    
    // Wacht op alle texture arrays tegelijk (parallel, niet sequentieel)
    for (auto& f : futures) {
        f.get();
    }
}

void Scene::spawn_heroes()
{
    //Transform hero_transform;
    //hero_transform.rotation = glm::quatLookAt(glm::vec3(0.f, 0.f, 1.f), glm::vec3(0.f, 1.f, 0.f));
    //hero_transform.scale = glm::vec3(1.f);


    int start_areas = 10;
    float start_area_tile_offset = 12.f;
    float spawn_start_y = terrain.tile_width * 3.f;

    float start_corner_y = 9.f * terrain.tile_width;
    float spawn_offset = terrain.tile_width / 3.f;
    float route_cache_resolution = terrain.tile_width * 6.f; // Verhoogd van 2 naar 6 voor minder unique routes

    std::cout << "Spawning characters and calculating routes..." << std::endl;

    std::vector<std::future<void>> futures;
    glm::uvec2 target = { 69 * terrain.tile_width, 160 * terrain.tile_width };

    for (int s = 0; s < start_areas; s++)
    {
        futures.push_back(pool.enqueue([this, s, spawn_offset, start_corner_y, spawn_start_y, start_area_tile_offset, target, route_cache_resolution] {
            std::vector<Hero> local_heroes;
            local_heroes.reserve(900); // Voorkom reallocaties

            float start_area_offset = s * start_area_tile_offset * terrain.tile_width;
            float base_x = start_corner_y + start_area_offset;

        for (int i = 0; i < 30; i++)
        {
            float x = base_x + (i * spawn_offset);
            for (int j = 0; j < 30; j++)
            {
                float z = spawn_start_y + (j * spawn_offset);
                float y = terrain.get_height(glm::vec2(x, z));
                glm::vec2 start_pos = glm::vec2(x, z);
                glm::ivec2 grid_pos = glm::ivec2(start_pos / route_cache_resolution);
                
                // Check globale cache eerst (thread-safe)
                std::vector<glm::vec2> route;
                {
                    std::lock_guard<std::mutex> lock(route_cache_mutex);
                    auto route_it = global_route_cache.find(grid_pos);
                    if (route_it != global_route_cache.end()) {
                        route = route_it->second;
                    } else {
                        route = terrain.find_route(start_pos, target);
                        global_route_cache[grid_pos] = route;
                    }
                }

                // Maak hero en set route
                local_heroes.emplace_back("frieren-blob", "frieren-blob", Transform(glm::vec3(x, y, z)), 20.f);
                local_heroes.back().set_route(route);
                }
            }

            // Voeg lokale lijst toe aan globale lijst, met mutex (vanwege thread safety)
            std::lock_guard<std::mutex> lock(hero_mutex);
            heroes.insert(heroes.end(), local_heroes.begin(), local_heroes.end());
            }));
    }

    // Wacht tot alle taken klaar zijn
    for (auto& f : futures) {
        f.get();
    }

    for(size_t i=0;i<heroes.size();i++)
    {
        hero_grid.add_hero(
            i,
            heroes[i].get_position2d()
        );
    }
    
   // Log::get_instance()->add_log("Spawned %d characters.\n", spawn_count);
}
void Scene::spawn_staves()
{
    glm::vec2 spawn_start{ terrain.tile_width * 15.f,  terrain.tile_length * 48.f };
    float height = terrain.get_height(spawn_start) + 50.f;

    float spawn_offset_x = 12.f * terrain.tile_height;
    float spawn_offset_y = 40.f * terrain.tile_length;

    float spawn_count = 0;
    for (int i = 0; i < 10; i++)
    {
        for (int j = 0; j < 2; j++)
        {
            spawn_count++;
            glm::vec3 position{ spawn_start.x + i * spawn_offset_x, height, spawn_start.y + j * spawn_offset_y };
            staves.emplace_back(position, &terrain);
        }
    }

    Log::get_instance()->add_log("Spawned %d staves.\n", spawn_count);
}

size_t Scene::get_character_count() const
{
    return heroes.size();
}

size_t Scene::get_staff_count() const
{
    return staves.size();

}

/**
 * Controleert botsingen tussen actieve helden en duwt ze uit elkaar indien nodig.
 */

void Scene::check_collisions()
{
    // 2. Buffers voorbereiden (Alleen resize als aantal helden verandert!)
    const size_t num_workers = 4; // Of std::thread::hardware_concurrency()
    if (collision_force_buffers.size() != num_workers || collision_force_buffers[0].size() != heroes.size())
    {
        collision_force_buffers.resize(num_workers, std::vector<glm::vec2>(heroes.size()));
    }

    // 3. Parallelle berekening (Zonder std::future / pool overhead)
    // We gebruiken een index-bereik om de parallelle uitvoering te sturen
    std::vector<size_t> worker_indices(num_workers);
    std::iota(worker_indices.begin(), worker_indices.end(), 0);

    std::for_each(std::execution::par, worker_indices.begin(), worker_indices.end(), [this, num_workers](size_t worker_id) {
        
        auto& forces = collision_force_buffers[worker_id];
        // Reset alleen onze eigen buffer
        std::fill(forces.begin(), forces.end(), glm::vec2{0.f});

        // Bereken chunk voor deze worker
        size_t chunk_size = (heroes.size() + num_workers - 1) / num_workers;
        size_t start = worker_id * chunk_size;
        size_t end = std::min(start + chunk_size, heroes.size());

        for (size_t i = start; i < end; ++i) {
            const auto& hero_i = heroes[i];
            if (!hero_i.is_active()) continue;

            const auto& nearby = hero_grid.get_nearby_heroes(hero_i.get_position2d());
            for (int j : nearby) {
                // Let op: we checken hier j (de buurman)
                if (i >= (size_t)j || !heroes[j].is_active()) continue;

                const auto& hero_j = heroes[j];
                
                // Squared distance check (sneller)
                const float radius_sum = hero_i.get_collision_radius() + hero_j.get_collision_radius();
                const glm::vec2 diff = hero_j.get_position2d() - hero_i.get_position2d();
                const float dist_sq = glm::dot(diff, diff);

                if (dist_sq < (radius_sum * radius_sum) && dist_sq > 0.0001f)
                {
                    const float dist = std::sqrt(dist_sq);

                    const glm::vec2 force =
                        glm::normalize(diff) * (radius_sum - dist);

                    forces[i] -= force;
                    forces[j] += force;
                }
            }
        }
    });

    // 4. Integratie (Accumuleren en toepassen)
    // Dit deel is snel genoeg single-threaded in vergelijking met de collision-fase
    for (size_t i = 0; i < heroes.size(); i++) {
        glm::vec2 total_force{ 0.f };
        for (size_t w = 0; w < num_workers; w++) {
            total_force += collision_force_buffers[w][i];
        }

        if (glm::length2(total_force) > 0.f) {
            heroes[i].apply_force(total_force);
        }
    }
}

#include <execution> // Voor parallelle uitvoering
void Scene::update(const float delta_time)
{
    const uint32_t phase = update_frame++ & 3u;
    
    handle_input(delta_time);
    
    // Camera volgen
    if (follow_mode && !heroes.empty())
    {
        const auto it = std::ranges::max_element(heroes, [](const Hero& a, const Hero& b)
            {
                return a.get_position().z < b.get_position().z;
            });
        static float last_z = std::numeric_limits<float>::lowest();
        const glm::vec3 hero_pos = it->get_position();
        if (std::abs(hero_pos.z - last_z) > 0.001f)
        {
            last_z = hero_pos.z;
            const glm::vec3 camera_pos{
                -28.0f,
                305.5f,
                hero_pos.z
            };
            camera.set_position(camera_pos);
            camera.set_direction(hero_pos - camera_pos);
        }
    }

    renderer->set_view_matrix(camera.get_view_matrix());

    // Heroes
    pool.parallel_for(
        heroes.size(),
        [&](size_t i)
        {
            heroes[i].update(delta_time, terrain);
        });
    
    // Collision + shield update slechts iedere 4 frames
    if(phase == 0)
    {
        check_collisions();
    }
    
    if(phase == 1)
    {
        pool.parallel_for(projectiles.size(), [&](size_t i)
        {
            projectiles[i].update(
                delta_time * 4.0f,
                camera,
                shield,
                heroes);
        });    }

    if(phase == 2)
    {
        shield.update(heroes);
    }
    
    if(phase == 3)
    {
        for(auto& l : active_lightning)
        {
            l.update(
                delta_time * 4.0f,
                camera,
                heroes);
        }
        
        for(auto& staff : staves)
        {
            staff.update(
                delta_time * 4.0f,
                heroes,
                active_lightning,
                projectiles);
        }
    }

    // Fast cleanup (volgorde niet behouden)
    auto cleanup = [](auto& container)
    {
        size_t i = 0;

        while (i < container.size())
        {
            if (!container[i].is_active())
            {
                container[i] = std::move(container.back());
                container.pop_back();
            }
            else
            {
                ++i;
            }
        }
    };

    if(!projectiles.empty())
        cleanup(projectiles);

    if(!active_lightning.empty())
        cleanup(active_lightning);
    

}

void Scene::draw()
{
    const size_t hero_count  = heroes.size();
    const size_t staff_count = staves.size();
    
    hero_transforms.resize(hero_count);
    staff_transforms.resize(staff_count);

    // --- OPTIMALISATIE 1: FRONT-TO-BACK SORTING ---
    // 1. Maak een lijst met indices en hun afstand-kwadraat tot de camera
    struct DistanceEntry {
        size_t original_index;
        float distance_sq;
    };
    
    std::vector<DistanceEntry> sorted_indices(hero_count);
    const glm::vec3 cam_pos = camera.get_position();

    // Snel de afstanden berekenen (dot product is sneller dan sqrt!)
    for (size_t i = 0; i < hero_count; ++i)
    {
        glm::vec3 diff = heroes[i].get_position() - cam_pos;
        sorted_indices[i] = { i, glm::dot(diff, diff) };
    }

    // Sorteer: dichtbij de camera eerst (<), zodat we maximaal profiteren van Early-Z
    std::sort(sorted_indices.begin(), sorted_indices.end(), [](const DistanceEntry& a, const DistanceEntry& b)
    {
        return a.distance_sq < b.distance_sq;
    });

    // 2. Haal de matrices parallel op, maar nu in de GESORTEERDE volgorde!
    pool.parallel_for(hero_count, [&](size_t i)
        {
            size_t sorted_hero_idx = sorted_indices[i].original_index;
            hero_transforms[i] = heroes[sorted_hero_idx].get_transform_matrix();
        });
    // ----------------------------------------------
    
    // Staff transforms (staven mogen gewoon parallel, die zijn niet zo zwaar)
    pool.parallel_for(staff_count, [&](size_t i)
        {
            staff_transforms[i] =
                staves[i].get_transform_matrix();
        });

    if (!hero_transforms.empty())
    {
        renderer->draw_batch(
            "frieren-blob",
            "frieren-blob",
            hero_transforms);
    }

    if (!staff_transforms.empty())
    {
        renderer->draw_batch(
            "staff",
            "staff",
            staff_transforms);
    }
    
    terrain.draw(renderer);

    for (const auto& lightning : active_lightning)
    {
        lightning.register_draw(lightning_sprite_manager);
    }

    lightning_sprite_manager.draw(renderer);
    lightning_sprite_manager.reset();

    for (const auto& projectile : projectiles)
    {
        projectile.register_draw(projectile_sprite_manager);
    }

    projectile_sprite_manager.draw(renderer);
    projectile_sprite_manager.reset();

    shield.draw(renderer);

    if (show_debug_windows)
    {
        show_health_values();
        show_mana_values();

        Log::get_instance()->draw("Log");

        show_controls();
    }
}
    
    
/// <summary>
/// Sorts all health values and displays them in a window.
/// </summary>
void Scene::show_health_values() const
{
    std::vector<int> health_values;
    health_values.reserve(heroes.size()); // Vermijd herhaaldelijk realloceren

    for (const auto& h : heroes)
    {
        health_values.push_back(h.get_health());
    }

    sort(health_values);

    ImGui::Begin("Heroes Health Bars");

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.90f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.f, 0.5f, 0.f, 1.0f }); //Green
    for (const int& hp : health_values)
    {
        char hp_text[16];
        snprintf(hp_text, sizeof(hp_text), "%d/1000", hp);
        ImGui::ProgressBar((float)hp / 1000, ImVec2(-FLT_MIN, 0.0f), hp_text);
    }
    ImGui::PopStyleColor(1);
    ImGui::SameLine(0.0f, ImGui::GetStyle().ItemInnerSpacing.x);

    ImGui::End();
}

/// <summary>
/// Sorts all mana values and displays them in a window.
/// </summary>
void Scene::show_mana_values() const
{
    std::vector<int> mana_values;
    mana_values.reserve(heroes.size()); // Vermijd herhaaldelijk realloceren

    for (const auto& s : heroes)
    {
        mana_values.push_back(s.get_mana());
    }

    sort(mana_values);

    ImGui::Begin("Heroes Mana Bars");

    ImGui::PushItemWidth(ImGui::GetWindowWidth() * 0.90f);
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, { 0.f, 0.f, 0.5f, 1.0f }); //Blue
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
    // als bereik leeg of 1 element bevat, is al gesorteerd
    if (low >= high) return;

    // kies pivot-element (midden van bereik)
    int pivot = arr[(low + high) / 2];
    int left = low, right = high;

    // verplaats elementen zodat kleinere links en grotere rechts van pivot komen
    while (left <= right)
    {
        // zoek van links naar rechts eerste element dat groter is dan pivot
        while (arr[left] < pivot) left++;

        // zoek van rechts naar links eerste element dat kleiner is dan pivot
        while (arr[right] > pivot) right--;

        // als left en right nog niet gekruist zijn, wissel elementen om
        if (left <= right)
        {
            std::swap(arr[left], arr[right]);
            left++;
            right--;
        }
    }

    // sorteer de twee deelarrays recursief
    quicksort(arr, low, right);  // linkerhelft (alle waarden < pivot)
    quicksort(arr, left, high);  // rechterhelft (alle waarden > pivot)
}

void Scene::handle_input(const float delta_time)
{
    const bool f1_pressed = glfwGetKey(renderer->get_window(), GLFW_KEY_F1) == GLFW_PRESS;
    if (f1_pressed && !f1_was_pressed) {
        show_debug_windows = !show_debug_windows;
    }
    f1_was_pressed = f1_pressed;

    //Toggle follow mode
    if (glfwGetKey(renderer->get_window(), GLFW_KEY_TAB) == GLFW_PRESS) { follow_mode = !follow_mode; }

    if (!follow_mode)
    {
        //Update camera on key presses
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

        //Only move the camera using the mouse when shift is pressed
        if (glfwGetKey(renderer->get_window(), GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
        {
            camera.update_direction(mouse_offset);
        }

    }
}

void Scene::show_controls()
{
    ImGui::Begin("Camera Controls Guide");

    // Follow Mode Toggle
    ImGui::Text("Follow Mode: %s", follow_mode ? "Enabled" : "Disabled");
    ImGui::Separator();

    // Movement Controls
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

    // Mouse Controls
    ImGui::Text("Mouse Controls:");
    ImGui::BulletText("[Mouse + SHIFT] - Look Around");

    ImGui::Separator();

    // Additional Info
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
