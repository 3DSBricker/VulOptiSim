#pragma once
#include <vector>
#include <cstddef>
#include <cstring>
#include <immintrin.h>
#include <cstdint>

namespace algo_utils {

    // Razendsnelle copy specifiek voor glm::mat4 (64 bytes = 2x 256-bit AVX registers)
    inline void fast_copy_mat4(glm::mat4* dest, const glm::mat4* src, size_t count) noexcept {
        float* d = reinterpret_cast<float*>(dest);
        const float* s = reinterpret_cast<const float*>(src);
        const size_t floats_to_copy = count * 16; 

        for (size_t i = 0; i < floats_to_copy; i += 16) {
            // Laad en store 2x 8 floats (64 bytes totaal per matrix)
            _mm256_storeu_ps(d + i, _mm256_loadu_ps(s + i));
            _mm256_storeu_ps(d + i + 8, _mm256_loadu_ps(s + i + 8));
        }
    }
    
    // Helper template om referenties te strippen (vervangt std::remove_reference)
    // Nodig om te voorkomen dat we per ongeluk een lvalue reference teruggeven.
    template<typename T> struct remove_reference { typedef T type; };
    template<typename T> struct remove_reference<T&> { typedef T type; };
    template<typename T> struct remove_reference<T&&> { typedef T type; };

    // Exacte, zero-overhead vervanger voor std::move
    // constexpr en noexcept zorgen voor maximale optimalisatie (0 instructies)
    template<typename T>
    constexpr typename remove_reference<T>::type&& move(T&& arg) noexcept {
        return static_cast<typename remove_reference<T>::type&&>(arg);
    }
    
    // Custom std::swap (geüpdatet met je eigen move)
    template<typename T>
    inline void swap(T& a, T& b) noexcept {
        T temp = algo_utils::move(a);
        a = algo_utils::move(b);
        b = algo_utils::move(temp);
    }

    // Volledig handgeschreven AVX2 memory copy zonder std::memcpy
    template<typename T>
    inline void fast_copy(T* dest, const T* src, size_t count) noexcept {
        size_t bytes = count * sizeof(T);
        uint8_t* d = reinterpret_cast<uint8_t*>(dest);
        const uint8_t* s = reinterpret_cast<const uint8_t*>(src);
        
        // Kopieer in blokken van 32 bytes (256-bit AVX)
        while (bytes >= 32) {
            _mm256_storeu_si256(
                reinterpret_cast<__m256i*>(d),
                _mm256_loadu_si256(reinterpret_cast<const __m256i*>(s))
            );
            d += 32;
            s += 32;
            bytes -= 32;
        }
        // Restant byte voor byte
        while (bytes--) {
            *d++ = *s++;
        }
    }

    // Custom vector equality test (vervangt std::equal)
    template<typename T>
    inline bool vectors_equal(const std::vector<T>& a, const std::vector<T>& b) noexcept {
        if (a.size() != b.size()) return false;
        const T* a_ptr = a.data();
        const T* b_ptr = b.data();
        const size_t count = a.size();
        for (size_t i = 0; i < count; ++i) {
            if (a_ptr[i] != b_ptr[i]) return false;
        }
        return true;
    }

    // Handgeschreven Quicksort
    template <typename T, typename Compare>
    inline void manual_quicksort(T* arr, int low, int high, Compare comp) {
        if (low >= high) return;
        int left = low;
        int right = high;
        T pivot = arr[(low + high) / 2];

        while (left <= right) {
            while (comp(arr[left], pivot)) left++;
            while (comp(pivot, arr[right])) right--;

            if (left <= right) {
                T temp = arr[left];
                arr[left] = arr[right];
                arr[right] = temp;
                left++;
                right--;
            }
        }
        manual_quicksort(arr, low, right, comp);
        manual_quicksort(arr, left, high, comp);
    }

    template <typename T>
    inline void parallel_sort(std::vector<T>& vec) {
        if (vec.empty()) return;
        manual_quicksort(vec.data(), 0, static_cast<int>(vec.size()) - 1, [](const T& a, const T& b) { return a < b; });
    }
}