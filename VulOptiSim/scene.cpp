#include "pch.h"
#include "scene.h"

Scene::Scene(vulvox::Renderer& renderer) : renderer(&renderer),
pool(std::min<size_t>(4, std::max(1u, std::thread::hardware_concurrency()))),
hero_grid(10.0f) // Grid om botsingsdetectie te optimaliseren (cell-size = 10 eenheden)
{
    std::vector<std::future<void>> future;
    glfwGetCursorPos(this->renderer->get_window(), &prev_mouse_pos.x, &prev_mouse_pos.y);

    glm::vec3 camera_pos{ -28.2815380f, 305.485260f, -30.0800228f };
    glm::vec3 camera_up{ 0.338442326f, 0.869414926f, 0.359964609f };
    glm::vec3 camera_direction{ 0.595541596f, -0.494082689f, 0.633413374f };

    camera = Camera(camera_pos, camera_up, camera_direction, 100.f, 100.f);

    terrain = Terrain(TERRAIN_PATH);

    shield = Shield{ "shield" };

    load_models_and_textures();

    future.push_back(pool.enqueue([this] {
        load_effects();
        }));

    future.push_back(pool.enqueue([this] {
        auto spawn_start = std::chrono::high_resolution_clock::now();
        spawn_heroes();
        auto spawn_end = std::chrono::high_resolution_clock::now();
        float spawn_duration = std::chrono::duration<float, std::chrono::milliseconds::period>(spawn_end - spawn_start).count();
        std::cout << "Spawn loading took: " << spawn_duration << " ms" << std::endl;
    }));

    future.push_back(pool.enqueue([this] {
        spawn_staves();
        }));

    // wacht tot alle taken klaar zijn
    for (auto& f : future) { // scheelt 3 seconden
        f.get();
    }

    std::cout << "Scene loaded." << std::endl;
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
    //Load all the models and textures we're going to need into GPU memory

    //Terrain textures
    std::vector<std::filesystem::path> texture_paths{
        CUBE_SEA_TEXTURE_PATH,  //Sea
        CUBE_GRASS_FLOWER_TEXTURE_PATH, //Lab floor
        CUBE_CONCRETE_WALL_TEXTURE_PATH, //Lab walls
        CUBE_MOSS_TEXTURE_PATH }; //Floor
    renderer->load_texture_array("texture_array_test", texture_paths);

    //Effects
    std::vector<std::filesystem::path> shield_path{ SHIELD_TEXTURE_PATH };
    renderer->load_texture_array("shield", shield_path);

    renderer->load_texture_array("lightning", LIGHTNING_TEXTURE_PATHS);

    renderer->load_texture_array("fireball", FIREBALL_TEXTURE_PATHS);
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
    float route_cache_resolution = terrain.tile_width * 2.f; // Grid-grootte voor routecache

    std::cout << "Spawning characters and calculating routes..." << std::endl;

    std::vector<std::future<void>> futures;
    glm::uvec2 target = { 69 * terrain.tile_width, 160 * terrain.tile_width };

    for (int s = 0; s < start_areas; s++)
    {
        futures.push_back(pool.enqueue([this, s, spawn_offset, start_corner_y, spawn_start_y, start_area_tile_offset, target, route_cache_resolution] {
            std::vector<Hero> local_heroes;
            local_heroes.reserve(900); // Voorkom reallocaties
            std::unordered_map<glm::ivec2, std::vector<glm::vec2>, IVec2Hash> route_cache;
            route_cache.reserve(64);

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
                glm::ivec2 grid_pos = glm::ivec2(start_pos / route_cache_resolution); // afgeronde grid positie
                
                    // check of route al bestaat
                    auto route_it = route_cache.find(grid_pos);
                    if (route_it == route_cache.end()) {
                        route_it = route_cache.emplace(grid_pos, terrain.find_route(start_pos, target)).first;
                    }

                    // maak hero en set route
                    local_heroes.emplace_back("frieren-blob", "frieren-blob", Transform(glm::vec3(x, y, z)), 20.f);
                    local_heroes.back().set_route(route_it->second);
                }
            }

            // voeg lokale lijst toe aan globale lijst, met mutex (vanwege thread safety)
            std::lock_guard<std::mutex> lock(hero_mutex);
            heroes.insert(heroes.end(), local_heroes.begin(), local_heroes.end());
            }));
    }

    // wacht tot alle taken klaar zijn
    for (auto& f : futures) {
        f.get();
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
    // Clear grid, deze manier vanwege reallocaties
    hero_grid.clear();

    // helden toevoegen
    for (size_t i = 0; i < heroes.size(); i++) {
        if (heroes[i].is_active()) {
            hero_grid.add_hero(i, heroes[i].get_position2d());
        }
    }

    const size_t worker_count = std::min<size_t>(4, std::max(1u, std::thread::hardware_concurrency()));
    const size_t batch_size = std::max<size_t>(1, (heroes.size() + worker_count - 1) / worker_count);

    if (collision_force_buffers.size() != worker_count)
    {
        collision_force_buffers.resize(worker_count);
    }

    for (auto& forces : collision_force_buffers)
    {
        forces.assign(heroes.size(), glm::vec2{ 0.f, 0.f });
    }

    std::vector<std::future<void>> futures;
    futures.reserve(worker_count);

    for (size_t worker = 0; worker < worker_count; worker++)
    {
        const size_t batch_start = worker * batch_size;
        const size_t batch_end = std::min(batch_start + batch_size, heroes.size());

        if (batch_start >= batch_end)
        {
            break;
        }

        futures.push_back(pool.enqueue([this, worker, batch_start, batch_end] {
            auto& forces = collision_force_buffers[worker];

            // Controleer botsingen binnen dezelfde grid-cellen
            for (size_t i = batch_start; i < batch_end; i++) {

                const auto& hero_i = heroes[i];  // maak referentie naar heroes[i]

                if (!hero_i.is_active()) continue;

                // Haal nabije helden op binnen dezelfde grid-cel
                const auto& nearby = hero_grid.get_nearby_heroes(hero_i.get_position2d());

                for (int j : nearby) {
                    const auto& hero_j = heroes[j];  // maak referentie naar heroes[j]

                    if (i == j || !hero_j.is_active()) continue; // Voorkom zelfbotsing en botsing met inactieve helden

                    // Controleer of de twee helden botsen
                    if (circle_collision(hero_i.get_position2d(), hero_i.get_collision_radius(),
                        hero_j.get_position2d(), hero_j.get_collision_radius()))
                    {
                        // Bereken duwrichting en kracht op basis van de overlap
                        glm::vec2 direction = hero_j.get_position2d() - hero_i.get_position2d();
                        const float distance = glm::length(direction);
                        if (distance > 0.0001f)
                        {
                            forces[j] += glm::normalize(direction) * ((hero_i.get_collision_radius()) - (distance / 2));
                        }
                    }
                }
            }
            }));
    }

    for (auto& f : futures) {
        f.get();
    }

    for (size_t hero_index = 0; hero_index < heroes.size(); hero_index++)
    {
        glm::vec2 force{ 0.f, 0.f };
        for (const auto& forces : collision_force_buffers)
        {
            force += forces[hero_index];
        }

        if (glm::length2(force) > 0.f)
        {
            heroes[hero_index].apply_force(force);
        }
    }

}

void Scene::update(const float delta_time)
{
    handle_input(delta_time);

    if (follow_mode)
    {
        auto it = std::ranges::max_element(heroes,
            [](const Hero& a, const Hero& b) {
                return a.get_position().z < b.get_position().z;
            }
        );

        glm::vec3 new_camera_position{ -28.0f, 305.5f, it->get_position().z };
        camera.set_position(new_camera_position);

        glm::vec3 camera_to_furthest = it->get_position() - camera.get_position();
        camera.set_direction(camera_to_furthest);
    }


    renderer->set_view_matrix(camera.get_view_matrix());

    // Collision is expensive and can run at a lower tick rate than movement/rendering.
    if (update_frame % 2 == 0)
    {
        check_collisions();
    }

    // Shield hull is expensive and does not need to be rebuilt every frame.
    if (update_frame % 4 == 0)
    {
        shield.update(heroes);
    }

    std::vector<std::future<void>> futures;

    const size_t worker_count = std::min<size_t>(4, std::max(1u, std::thread::hardware_concurrency()));
    const size_t hero_batch = std::max<size_t>(1, (heroes.size() + worker_count - 1) / worker_count);
    for (size_t i = 0; i < heroes.size(); i += hero_batch) {
        // Bereken het einde van de huidige batch
        size_t end = std::min(i + hero_batch, heroes.size());

        // Maak een thread voor elke batch
        futures.push_back(pool.enqueue([this, delta_time, end, i] {
            for (size_t j = i; j < end; ++j) {
                heroes[j].update(delta_time, terrain);
            }
            }));
    }

    // wacht
    for (auto& f : futures) {
        f.get();
    }

    for (auto& staff : staves)
    {
        staff.update(delta_time, heroes, active_lightning, projectiles);
    }

    for (auto& lightning : active_lightning)
    {
        lightning.update(delta_time, camera, heroes);
    }

    //Remove inactive lightning
    const auto [first_l, last_l] = std::ranges::remove_if(active_lightning, [](const Lightning& l) { return !l.is_active(); });
    active_lightning.erase(first_l, last_l);

    for (auto& projectile : projectiles)
    {
        projectile.update(delta_time, camera, shield, heroes);
    }

    //Remove inactive projectiles
    const auto [first_p, last_p] = std::ranges::remove_if(projectiles, [](const Projectile& p) { return !p.is_active(); });
    projectiles.erase(first_p, last_p);

    update_frame++;
}

void Scene::draw()
{
    //On using concurrency here:
    //  The graphics library copies the data to GPU memory so you can change the data after the draw functions of the renderer return.
    //  The actual drawing runs parallel to the host (CPU) execution.
    //  Make sure the data needed for drawing (position etc.) is ready before calling the corresponding draw functions or weird things happen.
    //  Calling draw functions outside of this functions lifetime will crash the program!


    hero_transforms.clear();
    hero_transforms.reserve(heroes.size());
    for (const auto& hero : heroes)
    {
        hero_transforms.push_back(hero.get_transform_matrix());
    }

    staff_transforms.clear();
    staff_transforms.reserve(staves.size());
    for (const auto& staff : staves)
    {
        staff_transforms.push_back(staff.get_transform_matrix());
    }


    // Verzend de transformaties in batches
    if (!hero_transforms.empty()) {
        renderer->draw_instanced("frieren-blob", "frieren-blob", hero_transforms);
    }

    if (!staff_transforms.empty()) {
        renderer->draw_instanced("staff", "staff", staff_transforms);
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
