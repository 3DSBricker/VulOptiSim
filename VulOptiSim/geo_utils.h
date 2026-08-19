#pragma once
#include <glm/glm.hpp>
#include <array>

namespace geo_utils {
    struct AABB {
        glm::vec3 min;
        glm::vec3 max;
    };

    struct Plane {
        glm::vec3 normal;
        float distance;
    };

    // Extract de 6 frustum planes uit een ViewProjection matrix
    inline std::array<Plane, 6> extract_frustum_planes(const glm::mat4& vp) noexcept {
        std::array<Plane, 6> planes;
        // Left, Right, Bottom, Top, Near, Far extractie (standaard Gamedev algoritme)
        for (int i = 0; i < 3; ++i) {
            planes[0].normal[i] = vp[i][3] + vp[i][0];
            planes[1].normal[i] = vp[i][3] - vp[i][0];
            planes[2].normal[i] = vp[i][3] + vp[i][1];
            planes[3].normal[i] = vp[i][3] - vp[i][1];
            planes[4].normal[i] = vp[i][3] + vp[i][2];
            planes[5].normal[i] = vp[i][3] - vp[i][2];
        }
        planes[0].distance = vp[3][3] + vp[3][0];
        planes[1].distance = vp[3][3] - vp[3][0];
        // ... (etc voor de rest, met normalisatie)
        return planes;
    }
    
    // Voeg dit toe in geo_utils.h onder is_aabb_in_frustum
    inline bool is_sphere_in_frustum(const glm::vec3& center, float radius, const std::array<Plane, 6>& planes) noexcept {
        for (const auto& plane : planes) {
            // Bepaal de afstand van het centrum van de sphere tot het frustum vlak
            float distance = (plane.normal.x * center.x) + 
                             (plane.normal.y * center.y) + 
                             (plane.normal.z * center.z) + plane.distance;
            
            // Als de negatieve afstand groter is dan de radius, ligt hij volledig buiten het vlak
            if (distance < -radius) {
                return false; 
            }
        }
        return true;
    }

    // Controleer of een AABB in het frustum valt (ideaal voor je TerrainChunks)
    inline bool is_aabb_in_frustum(const AABB& box, const std::array<Plane, 6>& planes) noexcept {
        for (const auto& plane : planes) {
            glm::vec3 p = box.min;
            if (plane.normal.x >= 0.0f) p.x = box.max.x;
            if (plane.normal.y >= 0.0f) p.y = box.max.y;
            if (plane.normal.z >= 0.0f) p.z = box.max.z;

            if (glm::dot(plane.normal, p) + plane.distance < 0.0f) {
                return false; // Ligt volledig buiten dit vlak
            }
        }
        return true;
    }
}