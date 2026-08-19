#pragma once
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/hash.hpp>
#include <vulkan/vulkan.h>
#include <cstdint>
#include <vector>

namespace vulvox
{
    struct TerrainVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 uv;
        uint32_t texture_array_index; // <-- Belangrijk voor je variërende terrein

        static std::vector<VkVertexInputBindingDescription> get_binding_description(uint32_t binding) {
            return {{ binding, sizeof(TerrainVertex), VK_VERTEX_INPUT_RATE_VERTEX }};
        }

        static std::vector<VkVertexInputAttributeDescription> get_attribute_descriptions(uint32_t binding) {
            std::vector<VkVertexInputAttributeDescription> attributes(4);
            attributes[0] = { 0, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TerrainVertex, position) };
            attributes[1] = { 1, binding, VK_FORMAT_R32G32B32_SFLOAT, offsetof(TerrainVertex, normal) };
            attributes[2] = { 2, binding, VK_FORMAT_R32G32_SFLOAT, offsetof(TerrainVertex, uv) };
            attributes[3] = { 3, binding, VK_FORMAT_R32_UINT, offsetof(TerrainVertex, texture_array_index) }; // Kan in shader een int/uint zijn
            return attributes;
        }
    };
    
    struct Vertex
    {
        glm::vec3 position;
        glm::vec3 color;
        glm::vec2 texture_coordinates;

        /// <summary>
        /// Returns a description of the input buffer containing vertices, including the memory stride.
        /// </summary>
        /// <returns></returns>
        static VkVertexInputBindingDescription get_binding_description(uint32_t binding);

        /// <summary>
        /// Defines an input attribute description for each of the member variables
        /// Each descriptor contains the format and byte offset with respect to the vertex struct
        /// </summary>
        /// <returns></returns>
        static std::vector<VkVertexInputAttributeDescription> get_attribute_descriptions(uint32_t binding);

        bool operator==(const Vertex& other) const;

    };
}

//Create hash function for vertices (useful for comparing in (unordered) maps)
namespace std
{
    template<> struct hash<vulvox::Vertex>
    {
        size_t operator()(vulvox::Vertex const& vertex) const
        {
            return ((hash<glm::vec3>()(vertex.position) ^
                (hash<glm::vec3>()(vertex.color) << 1)) >> 1) ^
                (hash<glm::vec2>()(vertex.texture_coordinates) << 1);
        }
    };
}