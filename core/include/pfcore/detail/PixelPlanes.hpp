#pragma once

#include <cstddef>
#include <cstdint>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif

namespace pfcore::detail {

// Exact byte rearrangement only. Preserve the planar representation used by
// the CPU decoder before the existing swscale resize/color conversion.
inline void unpackP010Luma(const std::uint8_t* input, std::uint8_t* output, std::size_t count)
{
    std::size_t x = 0;
#if defined(__SSE2__)
    for (; x + 8 <= count; x += 8) {
        const auto words = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + x * 2));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(output + x * 2), _mm_srli_epi16(words, 6));
    }
#endif
    for (; x < count; ++x) {
        const auto value = (unsigned(input[x * 2]) | (unsigned(input[x * 2 + 1]) << 8)) >> 6;
        output[x * 2] = static_cast<std::uint8_t>(value);
        output[x * 2 + 1] = static_cast<std::uint8_t>(value >> 8);
    }
}

inline void unpackP010Chroma(const std::uint8_t* input, std::uint8_t* u, std::uint8_t* v, std::size_t count)
{
    std::size_t x = 0;
#if defined(__SSE2__)
    const auto mask = _mm_set1_epi32(0x0000ffff);
    for (; x + 8 <= count; x += 8) {
        const auto a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + x * 4));
        const auto b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + x * 4 + 16));
        const auto ua = _mm_srli_epi32(_mm_and_si128(a, mask), 6);
        const auto ub = _mm_srli_epi32(_mm_and_si128(b, mask), 6);
        _mm_storeu_si128(reinterpret_cast<__m128i*>(u + x * 2), _mm_packs_epi32(ua, ub));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(v + x * 2),
            _mm_packs_epi32(_mm_srli_epi32(a, 22), _mm_srli_epi32(b, 22)));
    }
#endif
    for (; x < count; ++x) {
        unpackP010Luma(input + x * 4, u + x * 2, 1);
        unpackP010Luma(input + x * 4 + 2, v + x * 2, 1);
    }
}

inline void unpackNv12Chroma(const std::uint8_t* input, std::uint8_t* u, std::uint8_t* v, std::size_t count)
{
    std::size_t x = 0;
#if defined(__SSE2__)
    const auto mask = _mm_set1_epi16(0x00ff);
    for (; x + 16 <= count; x += 16) {
        const auto a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + x * 2));
        const auto b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(input + x * 2 + 16));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(u + x),
            _mm_packus_epi16(_mm_and_si128(a, mask), _mm_and_si128(b, mask)));
        _mm_storeu_si128(reinterpret_cast<__m128i*>(v + x),
            _mm_packus_epi16(_mm_srli_epi16(a, 8), _mm_srli_epi16(b, 8)));
    }
#endif
    for (; x < count; ++x) { u[x] = input[x * 2]; v[x] = input[x * 2 + 1]; }
}

} // namespace pfcore::detail
