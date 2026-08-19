#pragma once
#include <immintrin.h>
#include <glm/glm.hpp>

namespace math_utils {
    
    constexpr float FLT_MAX_VAL = 3.402823466e+38f;
    constexpr float FLT_MIN_VAL = 1.175494351e-38f; // Kleinste positieve waarde
    constexpr float FLT_LOWEST_VAL = -3.402823466e+38f; // Meest negatieve waarde
    
    constexpr int INT_MAX_VAL = 2147483647;
    constexpr int INT_MIN_VAL = -2147483648;

    template<typename T>
    inline constexpr T abs(T val) noexcept {
        return (val < T(0)) ? -val : val;
    }

    // Voor absolute afstand checks (voorkomt abs overhead)
    template<typename T>
    inline constexpr T abs_diff(T a, T b) noexcept {
        return (a > b) ? (a - b) : (b - a);
    }

    template<typename T>
    inline constexpr T min(T a, T b) noexcept {
        return (a < b) ? a : b;
    }

    template<typename T>
    inline constexpr T max(T a, T b) noexcept {
        return (a > b) ? a : b;
    }

    template<typename T>
    inline constexpr T clamp(T val, T low, T high) noexcept {
        return (val < low) ? low : ((val > high) ? high : val);
    }

    inline float fast_inv_sqrt(float number) noexcept {
        return _mm_cvtss_f32(_mm_rsqrt_ss(_mm_set_ss(number)));
    }

    inline bool is_within_radius(const glm::vec2& p1, const glm::vec2& p2, float radius) noexcept {
        float dx = p1.x - p2.x;
        float dy = p1.y - p2.y;
        return (dx * dx + dy * dy) <= (radius * radius);
    }

    template<typename T>
    inline T lerp(T a, T b, float t) noexcept {
        return a + t * (b - a);
    }
}