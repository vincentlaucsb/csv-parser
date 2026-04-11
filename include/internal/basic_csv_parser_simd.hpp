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
#include "common.hpp"

#if (defined(__AVX2__) || defined(__SSE2__)) && !defined(CSV_NO_SIMD)
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
        // Precomputed SIMD broadcast vectors for the four CSV sentinel bytes.
        // Constructed once per parser instance and passed by const-ref into
        // find_next_non_special, amortizing broadcast cost across every field
        // scan — meaningful for CSVs with many short fields.
        //
        // When no_quote mode is active, set quote_char = delimiter so that
        // quote bytes are not mistakenly treated as sentinels (they are
        // NOT_SPECIAL in that mode and must not cause SIMD to stop early).
        struct SentinelVecs {
            SentinelVecs() noexcept : SentinelVecs(',', '"') {}

            SentinelVecs(char delimiter, char quote_char) noexcept {
#if defined(__AVX2__) && !defined(CSV_NO_SIMD)
                v_delim = _mm256_set1_epi8(delimiter);
                v_quote = _mm256_set1_epi8(quote_char);
                v_lf    = _mm256_set1_epi8('\n');
                v_cr    = _mm256_set1_epi8('\r');
#elif defined(__SSE2__) && !defined(CSV_NO_SIMD)
                v_delim = _mm_set1_epi8(delimiter);
                v_quote = _mm_set1_epi8(quote_char);
                v_lf    = _mm_set1_epi8('\n');
                v_cr    = _mm_set1_epi8('\r');
#else
                (void)delimiter; (void)quote_char;
#endif
            }

#if defined(__AVX2__) && !defined(CSV_NO_SIMD)
            __m256i v_delim, v_quote, v_lf, v_cr;
#elif defined(__SSE2__) && !defined(CSV_NO_SIMD)
            __m128i v_delim, v_quote, v_lf, v_cr;
#endif
        };

        // Free function — easy to unit test independently of IBasicCSVParser.
        //
        // SIMD-only fast-forward: skips pos forward past any bytes that are
        // definitely not one of the four CSV sentinel characters. Stops as
        // soon as a sentinel byte is found OR fewer bytes remain than one
        // SIMD lane. The caller is responsible for the scalar tail loop using
        // compound_parse_flag, which correctly handles quote_escape state.
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
#if defined(__AVX2__) && !defined(CSV_NO_SIMD)
            while (pos + 32 <= data.size()) {
                __m256i bytes   = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(data.data() + pos));
                __m256i special = _mm256_cmpeq_epi8(bytes, sentinels.v_delim);
                special         = _mm256_or_si256(special, _mm256_cmpeq_epi8(bytes, sentinels.v_quote));
                special         = _mm256_or_si256(special, _mm256_cmpeq_epi8(bytes, sentinels.v_lf));
                special         = _mm256_or_si256(special, _mm256_cmpeq_epi8(bytes, sentinels.v_cr));
                int mask        = _mm256_movemask_epi8(special);

                if (mask != 0)
                    return pos + CSV_TZCNT32(static_cast<unsigned>(mask));
                pos += 32;
            }
#elif defined(__SSE2__) && !defined(CSV_NO_SIMD)
            while (pos + 16 <= data.size()) {
                __m128i bytes   = _mm_loadu_si128(reinterpret_cast<const __m128i*>(data.data() + pos));
                __m128i special = _mm_cmpeq_epi8(bytes, sentinels.v_delim);
                special         = _mm_or_si128(special, _mm_cmpeq_epi8(bytes, sentinels.v_quote));
                special         = _mm_or_si128(special, _mm_cmpeq_epi8(bytes, sentinels.v_lf));
                special         = _mm_or_si128(special, _mm_cmpeq_epi8(bytes, sentinels.v_cr));
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
