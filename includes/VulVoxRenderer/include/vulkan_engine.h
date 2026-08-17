#pragma once

// Standard / Library Includes
#include <vector>
#include <string>
#include <unordered_map>
#include <memory>
#include <chrono>
#include <filesystem>
#include <glm/glm.hpp>

// Engine Subsystem Includes
#include "vulkan_instance.h"
#include "renderer.h"
#include "vulkan_image.h"
#include "vulkan_swap_chain.h"
#include "vulkan_command_pool.h"
#include "vulkan_buffer_manager.h"
#include "mvp_handler.h"
#include "model.h"
#include "imgui_context.h" // Ensures ImGui_Context is declared

namespace vulvox
{
    // Zet dit bovenaan je Vulkan_Engine.h (buiten de class, of als private struct)
    struct StaticInstanceGroup {
        VkBuffer matrix_buffer = VK_NULL_HANDLE;
        VmaAllocation matrix_allocation = VK_NULL_HANDLE;
        uint32_t instance_count = 0;
    };
    
    class Vulkan_Engine
    {
    public:
        using StaticInstanceHandle = uint32_t;

        Vulkan_Engine();
        ~Vulkan_Engine();

        void init_window(uint32_t width, uint32_t height);
        void init_vulkan(const Renderer_Configuration& configuration);

        void init(uint32_t width, uint32_t height, const Renderer_Configuration& configuration);

        void init_imgui();
        void disable_imgui();
        ImGui_Context* get_imgui_context() const;
        
        struct TerrainInstanceData {
            glm::vec4 position_tex; // x,y,z = wereldpositie | w = texture index
            glm::vec4 scale_pad;    // x,y,z = scale | w = padding
        };

        uint32_t register_static_instances(const std::vector<TerrainInstanceData>& instance_data);
        void draw_static_instanced(const std::string& texture_array_name, Vulkan_Engine::StaticInstanceHandle handle);
        
        void destroy();

        GLFWwindow* get_glfw_window_ptr();
        MVP_Handler& get_mvp_handler();

        void resize_window(const uint32_t new_width, const uint32_t new_height);

        void load_model(const std::string& model_name, const std::filesystem::path& path);
        void load_mesh(const std::string& mesh_name, const std::vector<Vertex>& vertices, const std::vector<uint32_t>& indices);
        void load_texture(const std::string& texture_name, const std::filesystem::path& path);
        void load_texture_array(const std::string& texture_name, const std::vector<std::filesystem::path>& paths);

        void unload_model(const std::string& name);
        void unload_texture(const std::string& name);
        void unload_texture_array(const std::string& name);

        void start_draw();
        void end_draw();

        void draw_model(const std::string& model_name, const std::string& texture_name, const glm::mat4& model_matrix);
        void draw_mesh(const std::string& mesh_name, const std::string& texture_name, const glm::mat4& model_matrix);
        void draw_model_with_texture_array(const std::string& model_name, const std::string& texture_array_name, const int texture_index, const glm::mat4& model_matrix);
        void draw_batch(const std::string& model_name, const std::string& texture_name, const std::vector<glm::mat4>& transforms);
        void draw_instanced(const std::string& model_name, const std::string& texture_name, const std::vector<glm::mat4>& model_matrices);
        void draw_instanced_with_texture_array(const std::string& model_name, const std::string& texture_array_name, const std::vector<glm::mat4>& model_matrices, const std::vector<uint32_t>& texture_indices);
        void draw_planes(const std::string& texture_array_name, const std::vector<glm::mat4>& model_matrices, const std::vector<uint32_t>& texture_indices, const std::vector<glm::vec4>& min_max_uvs);
        void draw_terrain(const std::string& texture_array_name, const std::vector<glm::mat4>& model_matrices, const std::vector<uint32_t>& texture_indices, const std::vector<glm::vec4>& min_max_uvs);

        bool initialized() const;

        bool framebuffer_resized = false;

        std::string get_memory_statistics() const;
        const Frame_Statistics& get_frame_statistics() const;

    private:

        void update_uniform_buffer();

        //Swap chain recreation functions
        void recreate_swap_chain();
        void cleanup_swap_chain();
        
        VkPipeline static_terrain_pipeline = VK_NULL_HANDLE; // Specifiek voor de nieuwe struct

        void create_graphics_pipeline();

        // Administratie voor de statische groepen
        StaticInstanceHandle next_static_handle = 0;
        std::unordered_map<StaticInstanceHandle, StaticInstanceGroup> static_instance_groups;
        
        //Records the barriers + vkCmdBeginRendering/vkCmdEndRendering calls that used to be a
        //VkRenderPass + VkFramebuffer. Dynamic rendering (core Vulkan 1.3) needs no render pass
        //object and no framebuffer recreation on resize.
        void transition_swap_chain_image_to_color_attachment();
        void transition_swap_chain_image_to_present();

        //Depth test setup functions
        void create_depth_resources();

        void create_descriptor_pool();
        void create_mvp_descriptor_set_layout(); //Describes mvp uniform buffers
        void create_texture_descriptor_set_layout(); //Describes uniform image samplers
        void create_descriptor_sets();

        VkDescriptorSet create_texture_descriptor_set(const Image& texture);

        void create_sync_objects();
        void create_timestamp_queries();
        void read_gpu_timestamp(const uint32_t frame);
        void bind_pipeline(VkPipeline pipeline);
        void bind_descriptor_set(uint32_t set_index, VkDescriptorSet descriptor_set);
        void bind_vertex_buffer(uint32_t binding, VkBuffer buffer, VkDeviceSize offset);
        void bind_index_buffer(VkBuffer buffer);
        void reset_command_state_cache();

        void start_record_command_buffer();
        void end_record_command_buffer();
        
        template<typename T>
        void create_static_gpu_buffer(const std::vector<T>& data, VkBuffer& out_buffer, VmaAllocation& out_allocation)
        {
            VkDeviceSize buffer_size = sizeof(T) * data.size();
            if (buffer_size == 0) return;

            // 1. Maak de STAGING buffer aan (in traag CPU geheugen)
            VkBufferCreateInfo staging_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            staging_info.size = buffer_size;
            staging_info.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT; // We gaan hiervan kopiëren

            VmaAllocationCreateInfo staging_alloc_info = {};
            staging_alloc_info.usage = VMA_MEMORY_USAGE_CPU_ONLY; // Alleen voor de CPU

            VkBuffer staging_buffer;
            VmaAllocation staging_allocation;
            vmaCreateBuffer(vulkan_instance.allocator, &staging_info, &staging_alloc_info, &staging_buffer, &staging_allocation, nullptr);

            // 2. Kopieer data van RAM naar de Staging Buffer
            void* mapped_data;
            vmaMapMemory(vulkan_instance.allocator, staging_allocation, &mapped_data);
            std::memcpy(mapped_data, data.data(), buffer_size);
            vmaUnmapMemory(vulkan_instance.allocator, staging_allocation);

            // 3. Maak de ECHTE buffer aan op de GPU (Bliksemsnel VRAM)
            VkBufferCreateInfo buffer_info = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
            buffer_info.size = buffer_size;
            buffer_info.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT; // We gaan hierheen kopiëren

            VmaAllocationCreateInfo alloc_info = {};
            alloc_info.usage = VMA_MEMORY_USAGE_GPU_ONLY; // <-- DIT IS DE MAGIC FIX!

            vmaCreateBuffer(vulkan_instance.allocator, &buffer_info, &alloc_info, &out_buffer, &out_allocation, nullptr);

            // 4. Kopieer van Staging naar GPU
            // LET OP: Gebruik hier jouw engine's manier om een kopieer-commando uit te voeren. 
            // Heb je al een 'copy_buffer' helper functie in je Vulkan_Engine? Gebruik die dan zo:
            command_pool.copy_buffer(staging_buffer, out_buffer, buffer_size);

            // 5. Verwijder de tijdelijke staging buffer, we hebben hem niet meer nodig
            vmaDestroyBuffer(vulkan_instance.allocator, staging_buffer, staging_allocation);
        }

        VkShaderModule create_shader_module(const std::vector<char>& bytecode);

        bool has_stencil_component(VkFormat format) const;

        bool is_initialized = false;

        int frame_count = 0;

        uint32_t width = 800;
        uint32_t height = 600;
        GLFWwindow* window;

        //Vulkan and device contexts
        Vulkan_Instance vulkan_instance;

        Vulkan_Swap_Chain swap_chain;

        //Command pool and the allocated command buffers that store the commands send to the GPU
        Vulkan_Command_Pool command_pool;

        //Draw state
        VkCommandBuffer current_command_buffer;
        uint32_t current_image_index;

        //Semaphores and fences to synchronize the gpu and host operations
        std::vector<VkSemaphore> image_available_semaphores;
        std::vector<VkSemaphore> render_finished_semaphores;
        std::vector<VkFence> in_flight_fences; //Fence for draw finish
        VkQueryPool timestamp_query_pool = VK_NULL_HANDLE;

        VkDescriptorSetLayout mvp_descriptor_set_layout;
        VkDescriptorSetLayout texture_descriptor_set_layout;
        VkPipelineLayout pipeline_layout; //Describes the layout of the 'global' data, e.g. uniform buffers

        //GPU draw state (stages, shaders, rasterization options, depth settings, etc.)
        VkPipeline instance_pipeline;
        VkPipeline instance_tex_array_pipeline;
        VkPipeline vertex_pipeline;
        VkPipeline instance_plane_pipeline;
        VkPipeline instance_terrain_pipeline;

        struct Command_State_Cache
        {
            VkPipeline pipeline = VK_NULL_HANDLE;
            std::array<VkDescriptorSet, 2> descriptor_sets{ VK_NULL_HANDLE, VK_NULL_HANDLE };
            std::array<VkBuffer, 4> vertex_buffers{ VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE, VK_NULL_HANDLE };
            std::array<VkDeviceSize, 4> vertex_offsets{};
            VkBuffer index_buffer = VK_NULL_HANDLE;
        } command_state_cache;

        ///Stuff that gets send to the shaders



        //Descriptor pool and sets, 
        //holds the binding information that connects the shader inputs to the data
        VkDescriptorPool descriptor_pool;

        struct Descriptor_Sets
        {
            std::vector<VkDescriptorSet> tri_descriptor_set;
            std::vector<VkDescriptorSet> instance_descriptor_set;
        };

        Descriptor_Sets descriptor_sets;

        //Image used for depth testing
        Image depth_image;

        //Manages all the uniform and instance buffers
        Vulkan_Buffer_Manager buffer_manager;

        std::unordered_map<std::string, Model> models;
        std::unordered_map<std::string, Image> textures;
        std::unordered_map<std::string, VkDescriptorSet> texture_descriptor_sets;
        std::unordered_map<std::string, VkDescriptorSet> texture_array_descriptor_sets;
        std::unordered_map<std::string, Image> texture_arrays;

        MVP_Handler mvp_handler;

        //Optional user interface
        std::unique_ptr<ImGui_Context> imgui_context;

        //We don't want to wait for the previous frame to finish while processing the next frame,
        //so we create double the amount of buffers so we can overlap frame processing
        static const int MAX_FRAMES_IN_FLIGHT;
        uint32_t current_frame = 0;
        Frame_Statistics frame_statistics{};
        std::chrono::steady_clock::time_point cpu_frame_start{};
        std::chrono::steady_clock::time_point fps_sample_start{};
        uint32_t fps_sample_frame_count = 0;
        Renderer_Configuration configuration{};
    };


    static void framebuffer_resize_callback(GLFWwindow* window, int width, int height)
    {
        //Retrieve the object instance the window belongs to
        auto app = reinterpret_cast<Vulkan_Engine*>(glfwGetWindowUserPointer(window));
        app->framebuffer_resized = true;
    }
}
