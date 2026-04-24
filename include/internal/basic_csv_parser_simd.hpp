#pragma once
/** @file
 *  @brief SIMD-accelerated skip for runs of non-special CSV bytes.
 *
 *  Conservative design: any byte that could be the delimiter, quote character,
 *  \n, or \r causes an early return.
 *
 *  Uses 4x cmpeq rather than a lookup-table shuffle because CSV has only four
 *  sentinel characters. vpshufb (shuffle) truncates index bytes to their low
 *  nibble, causing aliasing across 16-byte boundaries and silently skipping
 *  real delimiters. The cmpeq approach is alias-free and equally fast for
 *  small sentinel sets.
 *
 *  UTF-8 safe: all CSV structural bytes are single-byte ASCII; multi-byte
 *  sequences (values > 0x7F) are never misidentified as special.
 */
#include <array>

#include "common.hpp"

#if !defined(CSV_NO_SIMD) && (defined(__AVX2__) || (defined(_MSC_VER) && defined(_M_AVX) && _M_AVX >= 2))
#define CSV_SIMD_AVX2 1
#elif !defined(CSV_NO_SIMD) && (defined(__SSE2__) || (defined(_MSC_VER) && (defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2))))
#define CSV_SIMD_SSE2 1
#endif

#if defined(CSV_SIMD_AVX2) || defined(CSV_SIMD_SSE2)
#include <immintrin.h>
// _tzcnt_u32 in GCC/Clang headers is __attribute__(__target__("bmi")), which
// requires -mbmi at the call site. __builtin_ctz has no such restriction and
// emits BSF/TZCNT as the optimizer sees fit. MSVC's _tzcnt_u32 has no
// equivalent restriction, so keep it there.
#  ifdef _MSC_VER
#    define CSV_TZCNT32(x) _tzcnt_u32(x)
#  else
#    define CSV_TZCNT32(x) static_cast<unsigned>(__builtin_ctz(x))
#  endif
#endif

namespace csv {
    namespace internals {
        // Precomputed byte vectors for the four CSV sentinel bytes.
        // Constructed once per parser instance and passed by const-ref into
        // find_next_non_special, amortizing fill cost across every field scan.
        //
        // Keep the layout independent of the consuming target's ISA macros.
        // CSVReader constructors are header-defined while parser methods live
        // in csv.lib, so a consumer TU and the library TU must agree on this
        // member layout even if only the library target was compiled with AVX2.
        //
        // Store byte arrays instead of __m256i/__m128i members so parser objects
        // do not carry over-aligned SIMD members on MSVC. The scan function uses
        // unaligned SIMD loads from these arrays.
        //
        // When no_quote mode is active, set quote_char = delimiter so that
        // quote bytes are not mistakenly treated as sentinels (they are
        // NOT_SPECIAL in that mode and must not cause SIMD to stop early).
        struct SentinelVecs {
            SentinelVecs() noexcept : SentinelVecs(',', '"') {}

            SentinelVecs(char delimiter, char quote_char) noexcept {
                v_delim.fill(delimiter);
                v_quote.fill(quote_char);
                v_lf.fill('\n');
                v_cr.fill('\r');
            }

            std::array<char, 32> v_delim, v_quote, v_lf, v_cr;
        };

        static_assert(sizeof(SentinelVecs) == 128, "SentinelVecs layout must stay ISA-independent.");
        static_assert(alignof(SentinelVecs) <= alignof(void*), "SentinelVecs must not require over-aligned allocation.");

        // Free function — easy to unit test independently of IBasicCSVParser.
        //
        // SIMD-only fast-forward: skips pos forward past any bytes that are
        // definitely not one of the four CSV sentinel characters. Stops as
        // soon as a sentinel byte is found OR fewer bytes remain than one
        // SIMD lane. The caller is responsible for the scalar tail loop.
        //
        // State-agnostic by design: stops conservatively at any sentinel byte
        // regardless of quote_escape. Inside a quoted field, delimiter and
        // newline bytes are NOT_SPECIAL under compound_parse_flag, so the
        // outer DFA loop re-enters parse_field immediately at zero cost.
        inline size_t find_next_non_special(
            csv::string_view data,
            size_t pos,
            const SentinelVecs& sentinels
        ) noexcept
        {
#if defined(CSV_SIMD_AVX2)
            const __m256i v_delim = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sentinels.v_delim.data()));
            const __m256i v_quote = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sentinels.v_quote.data()));
            const __m256i v_lf    = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sentinels.v_lf.data()));
            const __m256i v_cr    = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(sentinels.v_cr.data()));

            while (pos + 32 <= data.size()) {
                __m256i bytes   = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data.data() + pos));
                __m256i special = _mm256_cmpeq_epi8(bytes, v_delim);
                special         = _mm256_or_si256(special, _mm256_cmpeq_epi8(bytes, v_quote));
                special         = _mm256_or_si256(special, _mm256_cmpeq_epi8(bytes, v_lf));
                special         = _mm256_or_si256(special, _mm256_cmpeq_epi8(bytes, v_cr));
                int mask        = _mm256_movemask_epi8(special);

                if (mask != 0)
                    return pos + CSV_TZCNT32(static_cast<unsigned>(mask));
                pos += 32;
            }
#elif defined(CSV_SIMD_SSE2)
            const __m128i v_delim = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sentinels.v_delim.data()));
            const __m128i v_quote = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sentinels.v_quote.data()));
            const __m128i v_lf    = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sentinels.v_lf.data()));
            const __m128i v_cr    = _mm_loadu_si128(reinterpret_cast<const __m128i*>(sentinels.v_cr.data()));

            while (pos + 16 <= data.size()) {
                __m128i bytes   = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data.data() + pos));
                __m128i special = _mm_cmpeq_epi8(bytes, v_delim);
                special         = _mm_or_si128(special, _mm_cmpeq_epi8(bytes, v_quote));
                special         = _mm_or_si128(special, _mm_cmpeq_epi8(bytes, v_lf));
                special         = _mm_or_si128(special, _mm_cmpeq_epi8(bytes, v_cr));
                int mask        = _mm_movemask_epi8(special);

                if (mask != 0)
                    return pos + CSV_TZCNT32(static_cast<unsigned>(mask));
                pos += 16;
            }
#else
            (void)data; (void)sentinels;
#endif
            return pos;
        }
    }
}
