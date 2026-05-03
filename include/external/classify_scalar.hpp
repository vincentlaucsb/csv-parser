/*
classify_scalar, version 1.0.0
https://github.com/vincentlaucsb/classify_scalar

MIT License

Copyright (c) 2026 Vincent La

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#pragma once

#if defined(CLASSIFY_SCALAR_VERSION)
#if CLASSIFY_SCALAR_VERSION >= 10000
#define CLASSIFY_SCALAR_SKIP_HEADER
#else
#error "A newer classify_scalar.hpp was included after an older copy. Include the newest copy first."
#endif
#else
#define CLASSIFY_SCALAR_VERSION_MAJOR 1
#define CLASSIFY_SCALAR_VERSION_MINOR 0
#define CLASSIFY_SCALAR_VERSION_PATCH 0
#define CLASSIFY_SCALAR_VERSION 10000
#endif

#ifndef CLASSIFY_SCALAR_SKIP_HEADER

#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4127)
#endif

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <tuple>
#include <type_traits>

#if defined(_MSVC_LANG)
#define CLASSIFY_SCALAR_CPLUSPLUS _MSVC_LANG
#else
#define CLASSIFY_SCALAR_CPLUSPLUS __cplusplus
#endif

#if CLASSIFY_SCALAR_CPLUSPLUS >= 202002L
#define CLASSIFY_SCALAR_HAS_CXX20
#endif

#if CLASSIFY_SCALAR_CPLUSPLUS >= 201703L
#define CLASSIFY_SCALAR_HAS_CXX17
#endif

#if CLASSIFY_SCALAR_CPLUSPLUS >= 201402L
#define CLASSIFY_SCALAR_HAS_CXX14
#endif

#if defined(_WIN32) || defined(__LITTLE_ENDIAN__) \
    || (defined(__BYTE_ORDER__) && defined(__ORDER_LITTLE_ENDIAN__) && (__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__))
#define CLASSIFY_SCALAR_TRUE_U32 0x65757274U
#define CLASSIFY_SCALAR_FALSE_PREFIX_U32 0x736c6166U
#else
#define CLASSIFY_SCALAR_TRUE_U32 0x74727565U
#define CLASSIFY_SCALAR_FALSE_PREFIX_U32 0x66616c73U
#endif

#if defined(__clang__) || defined(__GNUC__)
#define CLASSIFY_SCALAR_CONST __attribute__((__const__))
#else
#define CLASSIFY_SCALAR_CONST
#endif

#if defined(_MSC_VER)
#define CLASSIFY_SCALAR_FORCE_INLINE __forceinline
#elif defined(__clang__) || defined(__GNUC__)
#define CLASSIFY_SCALAR_FORCE_INLINE inline __attribute__((__always_inline__))
#else
#define CLASSIFY_SCALAR_FORCE_INLINE inline
#endif

#ifdef CLASSIFY_SCALAR_HAS_CXX14
#define CLASSIFY_SCALAR_CONSTEXPR_14 constexpr
#define CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 constexpr
#define CLASSIFY_SCALAR_LOCAL_TABLE_VALUE_14 constexpr
#else
#define CLASSIFY_SCALAR_CONSTEXPR_14 inline
#define CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 const
// C++11 cannot constexpr-build the lookup tables below, so local statics must
// stay mutable while their runtime initializer writes table entries.
#define CLASSIFY_SCALAR_LOCAL_TABLE_VALUE_14
#endif

#ifdef CLASSIFY_SCALAR_HAS_CXX17
#define IF_CONSTEXPR if constexpr
#define CLASSIFY_SCALAR_CONSTEXPR_17 constexpr
#define CLASSIFY_SCALAR_CONSTEXPR_VALUE_17 constexpr
#else
#define IF_CONSTEXPR if
#define CLASSIFY_SCALAR_CONSTEXPR_17 inline
#define CLASSIFY_SCALAR_CONSTEXPR_VALUE_17 const
#endif

#ifdef CLASSIFY_SCALAR_HAS_CXX20
#include <concepts>
#endif

#if CLASSIFY_SCALAR_CPLUSPLUS >= 201703L
#include <charconv>
#include <string_view>
#include <system_error>
#endif

#if defined(CLASSIFY_SCALAR_HAS_CXX17) && !defined(_LIBCPP_VERSION)
#define CLASSIFY_SCALAR_HAS_STD_FLOAT_FROM_CHARS
#endif

namespace classify_scalar {

/// Built-in scalar classification ids returned by classify_scalar().
enum ScalarKind : int {
    /// Empty input after optional ASCII trimming.
    scalar_null = 0,
    /// Input did not match any enabled scalar policy.
    scalar_string = 1,
    /// Case-insensitive "true" or "false".
    scalar_bool = 2,
    /// Signed 8-bit integer, including 0x-prefixed hexadecimal.
    scalar_int8 = 3,
    /// Reserved for future unsigned 8-bit integer classification.
    scalar_uint8 = 4,
    /// Signed 16-bit integer, including 0x-prefixed hexadecimal.
    scalar_int16 = 5,
    /// Reserved for future unsigned 16-bit integer classification.
    scalar_uint16 = 6,
    /// Signed 32-bit integer, including 0x-prefixed hexadecimal.
    scalar_int32 = 7,
    /// Reserved for future unsigned 32-bit integer classification.
    scalar_uint32 = 8,
    /// Signed 64-bit integer, including 0x-prefixed hexadecimal.
    scalar_int64 = 9,
    /// Reserved for future unsigned 64-bit integer classification.
    scalar_uint64 = 10,
    /// Well-formed decimal integer outside the int64 range.
    scalar_bigint = 11,
    /// Floating-point literal parsed as double.
    scalar_float = 12,
	/// High precision floating-point literal (reserved for future use, not currently returned by classify_scalar).
    scalar_bigfloat = 13,
    /// Conservative ISO date/date-time value, stored as UTC unix milliseconds when parsed.
    scalar_timestamp = 14,
    /// Reserved sentinel for invalid integration results.
    scalar_invalid = -2,
    /// First id available for user-defined scalar kinds.
    scalar_custom_begin = 1024
};

/// Include these entries at the top of a custom scalar enum.
#define CLASSIFY_SCALAR_BUILTINS \
    scalar_null = ::classify_scalar::scalar_null, \
    scalar_string = ::classify_scalar::scalar_string, \
    scalar_bool = ::classify_scalar::scalar_bool, \
    scalar_int8 = ::classify_scalar::scalar_int8, \
    scalar_uint8 = ::classify_scalar::scalar_uint8, \
    scalar_int16 = ::classify_scalar::scalar_int16, \
    scalar_uint16 = ::classify_scalar::scalar_uint16, \
    scalar_int32 = ::classify_scalar::scalar_int32, \
    scalar_uint32 = ::classify_scalar::scalar_uint32, \
    scalar_int64 = ::classify_scalar::scalar_int64, \
    scalar_uint64 = ::classify_scalar::scalar_uint64, \
    scalar_bigint = ::classify_scalar::scalar_bigint, \
    scalar_float = ::classify_scalar::scalar_float, \
    scalar_bigfloat = ::classify_scalar::scalar_bigfloat, \
    scalar_timestamp = ::classify_scalar::scalar_timestamp, \
    scalar_invalid = ::classify_scalar::scalar_invalid, \
    scalar_custom_begin = ::classify_scalar::scalar_custom_begin - 1

/// Non-owning half-open character span [first, last).
struct scalar_span {
    scalar_span() noexcept : first(nullptr), last(nullptr) {}
    scalar_span(const char* first_, const char* last_) noexcept : first(first_), last(last_) {}

    const char* first;
    const char* last;
};

/// Output object used when only the scalar kind is needed.
struct classify_only_output {
    template<ScalarKind, typename T>
    void set(T) const noexcept {}
};

namespace detail {
namespace integer {

/// Mapping of integer kinds to their rank for easy comparison.
CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 std::array<unsigned, 9> kind_rank_table = {{
    1U, // scalar_int8
    1U, // scalar_uint8
    2U, // scalar_int16
    2U, // scalar_uint16
    3U, // scalar_int32
    3U, // scalar_uint32
    4U, // scalar_int64
    4U, // scalar_uint64
    5U  // scalar_bigint
}};

/// True when kind is one of the built-in signed integer widths.
CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 bool is_signed_integer_kind(ScalarKind kind) noexcept {
    return kind == scalar_int8 || kind == scalar_int16 || kind == scalar_int32 || kind == scalar_int64;
}

/// True when kind is one of the reserved unsigned integer widths.
CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 bool is_unsigned_integer_kind(ScalarKind kind) noexcept {
    return kind == scalar_uint8 || kind == scalar_uint16 || kind == scalar_uint32 || kind == scalar_uint64;
}

/// True when kind is any built-in integer or bigint classification.
CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 bool is_integer_kind(ScalarKind kind) noexcept {
    return is_signed_integer_kind(kind) || is_unsigned_integer_kind(kind) || kind == scalar_bigint;
}

/// Rank integer widths from smallest to largest; non-integers return 0.
CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 unsigned integer_kind_rank(ScalarKind kind) noexcept {
    constexpr auto offset = static_cast<unsigned>(scalar_int8);
    return kind >= scalar_int8 && kind <= scalar_bigint ?
         kind_rank_table[static_cast<unsigned>(kind) - offset] : 0U;
}

/// True when parsed_integer_kind can be stored by target_integer_kind.
CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 bool integer_kind_fits_in(
    ScalarKind parsed_integer_kind,
    ScalarKind target_integer_kind) noexcept {
    return is_signed_integer_kind(parsed_integer_kind)
        && is_signed_integer_kind(target_integer_kind)
        && integer_kind_rank(parsed_integer_kind) <= integer_kind_rank(target_integer_kind);
}

/// Return the narrowest signed integer scalar kind that can store value.
CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 ScalarKind classify_integer_kind(std::int64_t value) noexcept {
    if (value >= 0) {
        return value <= static_cast<std::int64_t>(std::numeric_limits<std::int8_t>::max())
            ? scalar_int8
            : value <= static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::max())
            ? scalar_int16
            : value <= static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::max())
            ? scalar_int32
            : scalar_int64;
    }

    return value >= static_cast<std::int64_t>(std::numeric_limits<std::int8_t>::min())
        ? scalar_int8
        : value >= static_cast<std::int64_t>(std::numeric_limits<std::int16_t>::min())
        ? scalar_int16
        : value >= static_cast<std::int64_t>(std::numeric_limits<std::int32_t>::min())
        ? scalar_int32
        : scalar_int64;
}

} // namespace integer
} // namespace detail

/// Built-in output adapter for numeric, bool, and timestamp storage.
struct builtin_output_refs {
    builtin_output_refs(long double& number_, std::int64_t& integer_, bool& boolean_) noexcept
        : number(number_), integer(integer_), boolean(boolean_), timestamp(nullptr) {}

    builtin_output_refs(
        long double& number_,
        std::int64_t& integer_,
        bool& boolean_,
        std::uint64_t& timestamp_) noexcept
        : number(number_),
          integer(integer_),
          boolean(boolean_),
          timestamp(&timestamp_) {}

    template<ScalarKind Kind>
    typename std::enable_if<Kind >= scalar_int8 && Kind <= scalar_int64 && ((Kind - scalar_int8) % 2 == 0), void>::type
    set(std::int64_t value) const noexcept {
        integer = value;
    }

    template<ScalarKind Kind>
    typename std::enable_if<Kind == scalar_float, void>::type set(long double value) const noexcept {
        number = value;
    }

    template<ScalarKind Kind>
    typename std::enable_if<Kind == scalar_bool, void>::type set(bool value) const noexcept {
        boolean = value;
    }

    template<ScalarKind Kind>
    typename std::enable_if<Kind == scalar_timestamp, void>::type set(std::uint64_t value) const noexcept {
        if (timestamp)
            *timestamp = value;
    }

    long double& number;
    std::int64_t& integer;
    bool& boolean;
    std::uint64_t* timestamp;
};

/// Create the standard built-in output adapter.
CLASSIFY_SCALAR_FORCE_INLINE builtin_output_refs output_refs(long double& number, std::int64_t& integer, bool& boolean) noexcept {
    return builtin_output_refs(number, integer, boolean);
}

enum class ParseFlag : unsigned char {
    /// Any byte with no scalar-classification meaning in the active parse table.
    other,
    /// ASCII digit bytes '0' through '9'.
    digit,
    /// Active decimal separator byte, '.' by default.
    decimal
};

namespace detail {

template<typename Enum>
struct has_custom_scalar_begin {
    template<typename T>
    static char test(decltype(T::scalar_custom_begin)*);

    template<typename>
    static long test(...);

    enum { value = sizeof(test<Enum>(nullptr)) == sizeof(char) };
};

template<typename Enum, bool HasCustomBegin>
struct custom_scalar_enum_guard {
    static_assert(HasCustomBegin, "custom scalar enums should include CLASSIFY_SCALAR_BUILTINS");

    static constexpr int cast_to_int(Enum value) noexcept {
        return static_cast<int>(value);
    }

    static constexpr int cast_scalar_kind(ScalarKind value) noexcept {
        return static_cast<int>(value);
    }
};

template<typename Enum>
struct custom_scalar_enum_guard<Enum, true> {
    static_assert(
        static_cast<int>(Enum::scalar_custom_begin) == scalar_custom_begin - 1,
        "custom scalar enum has an invalid scalar_custom_begin sentinel");

    static constexpr int cast_to_int(Enum value) noexcept {
        return static_cast<int>(value);
    }

    static constexpr int cast_scalar_kind(ScalarKind value) noexcept {
        return static_cast<int>(value);
    }
};

template<typename Kind, bool IsScalarKind>
struct scalar_kind_cast_impl;

template<typename Kind>
struct scalar_kind_cast_impl<Kind, false> {
    static_assert(std::is_enum<Kind>::value, "custom scalar kind type must be an enum");

    static constexpr Kind from_scalar_kind(ScalarKind value) noexcept {
        return static_cast<Kind>(custom_scalar_enum_guard<
            Kind,
            has_custom_scalar_begin<Kind>::value>::cast_scalar_kind(value));
    }

    template<typename Enum>
    static constexpr Kind from_enum(Enum value) noexcept {
        return static_cast<Kind>(custom_scalar_enum_guard<
            Enum,
            has_custom_scalar_begin<Enum>::value>::cast_to_int(value));
    }
};

template<typename Kind>
struct scalar_kind_cast_impl<Kind, true> {
    static constexpr ScalarKind from_scalar_kind(ScalarKind value) noexcept {
        return value;
    }

    template<typename Enum>
    static constexpr ScalarKind from_enum(Enum value) noexcept {
        return static_cast<ScalarKind>(custom_scalar_enum_guard<
            Enum,
            has_custom_scalar_begin<Enum>::value>::cast_to_int(value));
    }
};

template<typename Kind>
constexpr Kind scalar_kind_cast(ScalarKind value) noexcept {
    return scalar_kind_cast_impl<
        Kind,
        std::is_same<Kind, ScalarKind>::value>::from_scalar_kind(value);
}

template<typename Kind, typename Enum>
constexpr typename std::enable_if<std::is_enum<Enum>::value && !std::is_same<Enum, ScalarKind>::value, Kind>::type
scalar_kind_cast(Enum value) noexcept {
    return scalar_kind_cast_impl<
        Kind,
        std::is_same<Kind, ScalarKind>::value>::from_enum(value);
}

CLASSIFY_SCALAR_FORCE_INLINE bool is_ascii_space(const char c) noexcept {
    static CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 bool table[256] = {
        false, false, false, false, false, false, false, false,
        false, true, true, true, true, true, false, false,
        false, false, false, false, false, false, false, false,
        false, false, false, false, false, false, false, false,
        true
    };
    return table[static_cast<unsigned char>(c)];
}

template<char DecimalSymbol>
CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 ParseFlag classify_ascii_char(const unsigned char c) noexcept {
    return c >= '0' && c <= '9' ? ParseFlag::digit
        : c == static_cast<unsigned char>(DecimalSymbol) ? ParseFlag::decimal
        : ParseFlag::other;
}

template<std::size_t... Indexes>
struct index_sequence {};

template<std::size_t Count, std::size_t... Indexes>
struct make_index_sequence_impl : make_index_sequence_impl<Count - 1, Count - 1, Indexes...> {};

template<std::size_t... Indexes>
struct make_index_sequence_impl<0, Indexes...> {
    typedef index_sequence<Indexes...> type;
};

template<std::size_t Count>
struct make_index_sequence {
    typedef typename make_index_sequence_impl<Count>::type type;
};

struct parse_table_type {
    ParseFlag values[256];

    CLASSIFY_SCALAR_CONSTEXPR_14 ParseFlag operator[](unsigned char value) const noexcept {
        return values[value];
    }
};

struct dispatch_table_type {
    unsigned char values[256];

    CLASSIFY_SCALAR_CONSTEXPR_14 unsigned char operator[](unsigned char value) const noexcept {
        return values[value];
    }
};

struct parse_state {
    enum Sign : unsigned char {
        no_sign,
        positive_sign,
        negative_sign
    };

    parse_state(const char* first_, const char* last_) noexcept
        : first(first_),
          last(last_),
          current(first_),
          numeric_first(first_),
          sign(no_sign) {}

    const char* first;
    const char* last;
    const char* current;
    const char* numeric_first;
    Sign sign;
};

#ifdef CLASSIFY_SCALAR_HAS_CXX20
template<typename Policy>
concept scalar_policy = requires(
    unsigned char c,
    const Policy& policy,
    parse_state& state,
    classify_only_output& output) {
    { Policy::matches_leading(c) } -> std::convertible_to<bool>;
    policy.on_dispatch(state, output);
};
#endif

CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 unsigned char no_dispatch_policy = 255U;

template<unsigned char Index, typename... Policies>
struct dispatch_index_impl;

template<unsigned char Index>
struct dispatch_index_impl<Index> {
    CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 static unsigned char value(unsigned char) noexcept {
        return no_dispatch_policy;
    }
};

template<unsigned char Index, typename First, typename... Rest>
struct dispatch_index_impl<Index, First, Rest...> {
    CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 static unsigned char value(unsigned char c) noexcept {
        return First::matches_leading(c)
            ? Index
            : dispatch_index_impl<static_cast<unsigned char>(Index + 1U), Rest...>::value(c);
    }
};

template<std::size_t Index, std::size_t Count>
struct policy_dispatch_impl {
    template<typename Kind, typename Tuple, typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE static Kind call(
        const unsigned char policy_index,
        const Tuple& policies,
        parse_state& state,
        Output& output) noexcept {
        typedef typename std::remove_reference<Tuple>::type tuple_type;
        typedef typename std::tuple_element<Index, tuple_type>::type policy_type;

        if (policy_index > Index)
            return policy_dispatch_impl<Index + 1U, Count>::template call<Kind>(policy_index, policies, state, output);

        if (policy_index == Index || policy_type::matches_leading(static_cast<unsigned char>(*state.first))) {
            const Kind kind = scalar_kind_cast<Kind>(std::get<Index>(policies).on_dispatch(state, output));
            if (kind != scalar_kind_cast<Kind>(scalar_string))
                return kind;
        }

        return policy_dispatch_impl<Index + 1U, Count>::template call<Kind>(policy_index, policies, state, output);
    }
};

template<std::size_t Count>
struct policy_dispatch_impl<Count, Count> {
    template<typename Kind, typename Tuple, typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE static Kind call(
        unsigned char,
        const Tuple&,
        parse_state&,
        Output&) noexcept {
        return scalar_kind_cast<Kind>(scalar_string);
    }
};

template<typename... Policies>
#ifdef CLASSIFY_SCALAR_HAS_CXX20
requires (scalar_policy<Policies> && ...)
#endif
struct policy_pack {
    typedef std::tuple<Policies...> tuple_type;

    policy_pack() noexcept : policies() {}

    explicit policy_pack(Policies... policies_) noexcept
        : policies(policies_...) {}

    CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 static unsigned char dispatch_index(unsigned char c) noexcept {
        return dispatch_index_impl<0U, Policies...>::value(c);
    }

    template<typename Kind = ScalarKind, typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE Kind dispatch(
        const unsigned char policy_index,
        parse_state& state,
        Output& output) const noexcept {
        return policy_dispatch_impl<0U, sizeof...(Policies)>::template call<Kind>(policy_index, policies, state, output);
    }

    tuple_type policies;
};

template<char DecimalSymbol, std::size_t... Indexes>
CLASSIFY_SCALAR_CONSTEXPR_14 parse_table_type build_parse_table(index_sequence<Indexes...>) noexcept {
    return parse_table_type{{classify_ascii_char<DecimalSymbol>(static_cast<unsigned char>(Indexes))...}};
}

template<char DecimalSymbol>
CLASSIFY_SCALAR_FORCE_INLINE const parse_table_type& parse_table() noexcept {
    static CLASSIFY_SCALAR_LOCAL_TABLE_VALUE_14 parse_table_type table =
        build_parse_table<DecimalSymbol>(typename make_index_sequence<256>::type());
    return table;
}

template<typename PolicyPack, std::size_t... Indexes>
CLASSIFY_SCALAR_CONSTEXPR_14 dispatch_table_type build_dispatch_table(index_sequence<Indexes...>) noexcept {
    return dispatch_table_type{{PolicyPack::dispatch_index(static_cast<unsigned char>(Indexes))...}};
}

template<typename PolicyPack>
CLASSIFY_SCALAR_FORCE_INLINE const dispatch_table_type& dispatch_table() noexcept {
    static CLASSIFY_SCALAR_LOCAL_TABLE_VALUE_14 dispatch_table_type table =
        build_dispatch_table<PolicyPack>(typename make_index_sequence<256>::type());
    return table;
}

CLASSIFY_SCALAR_CONSTEXPR_17 std::array<char, 256> create_ascii_lower_table() noexcept {
    std::array<char, 256> table = {};
    for (std::size_t i = 0; i < table.size(); ++i) {
        table[i] = static_cast<char>(i);
    }
    for (unsigned char i = 0; i < 26; ++i) {
        table[static_cast<unsigned char>('A' + i)] = static_cast<char>('a' + i);
    }
    return table;
}

CLASSIFY_SCALAR_CONSTEXPR_VALUE_17 std::array<char, 256> ascii_lower_chars = create_ascii_lower_table();

CLASSIFY_SCALAR_CONSTEXPR_17 std::array<bool, 256> create_ascii_digits_table() noexcept {
    std::array<bool, 256> table = {};
    for (unsigned char i = 0; i < 10; ++i) {
        table[static_cast<unsigned char>('0' + i)] = true;
    }
    return table;
}

CLASSIFY_SCALAR_CONSTEXPR_VALUE_17 std::array<bool, 256> ascii_digits = create_ascii_digits_table();

CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 unsigned char invalid_digit_value = 255U;

CLASSIFY_SCALAR_CONSTEXPR_17 std::array<unsigned char, 256> create_digit_values_table() noexcept {
    std::array<unsigned char, 256> table = {};
    for (std::size_t i = 0; i < table.size(); ++i) {
        table[i] = invalid_digit_value;
    }
    for (unsigned char i = 0; i < 10; ++i) {
        table[static_cast<unsigned char>('0' + i)] = i;
    }
    for (unsigned char i = 0; i < 26; ++i) {
        table[static_cast<unsigned char>('A' + i)] = static_cast<unsigned char>(10U + i);
        table[static_cast<unsigned char>('a' + i)] = static_cast<unsigned char>(10U + i);
    }
    return table;
}

CLASSIFY_SCALAR_CONSTEXPR_VALUE_17 std::array<unsigned char, 256> digit_values = create_digit_values_table();

CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 std::int64_t int64_min_value = std::numeric_limits<std::int64_t>::min();
CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 std::int64_t int64_max_value = std::numeric_limits<std::int64_t>::max();
CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 std::uint64_t int64_positive_limit = static_cast<std::uint64_t>(int64_max_value);
CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 std::uint64_t int64_negative_limit = int64_positive_limit + 1U;
CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 std::uint64_t signed_integer_limits[3] = {
    int64_positive_limit,
    int64_positive_limit,
    int64_negative_limit
};
CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 long double int64_min_long_double = static_cast<long double>(int64_min_value);
CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 long double int64_max_long_double = static_cast<long double>(int64_max_value);

namespace parsing {

CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 parse_state::Sign parse_sign(unsigned char c) noexcept {
    return c == static_cast<unsigned char>('+') ? parse_state::positive_sign :
        c == static_cast<unsigned char>('-') ? parse_state::negative_sign :
        parse_state::no_sign;
}

CLASSIFY_SCALAR_FORCE_INLINE const char* apply_leading_sign(parse_state& state) noexcept {
    const unsigned char first_char = static_cast<unsigned char>(*state.first);
    const parse_state::Sign sign = parse_sign(first_char);
    if (sign != parse_state::no_sign) {
        state.sign = sign;
        state.numeric_first = state.sign == parse_state::negative_sign
            ? state.first
            : state.first + 1;
        return state.first + 1;
    }

    return state.first;
}

CLASSIFY_SCALAR_FORCE_INLINE std::uint32_t load_u32(const char* value) noexcept {
    std::uint32_t word = 0;
    std::memcpy(&word, value, sizeof(word));
    return word;
}

CLASSIFY_SCALAR_FORCE_INLINE unsigned char decimal_digit_value(const unsigned char c) noexcept {
    // The digit_values[] lookup is useful for generic-base parsing, but benchmarked slower in the hot decimal scanner.
    return static_cast<unsigned char>(c - static_cast<unsigned char>('0'));
}

CLASSIFY_SCALAR_FORCE_INLINE scalar_span trim_ascii(const char* first, const char* last) noexcept {
    while (first != last && is_ascii_space(*first))
        ++first;

    while (first != last && is_ascii_space(*(last - 1)))
        --last;

    return scalar_span{first, last};
}

template<bool TrimAsciiWhitespace>
CLASSIFY_SCALAR_FORCE_INLINE scalar_span trim_span(const char* first, const char* last) noexcept {
    if (!first || !last || last < first)
        return scalar_span();

    IF_CONSTEXPR(TrimAsciiWhitespace)
        return trim_ascii(first, last);
    else
        return scalar_span(first, last);
}

CLASSIFY_SCALAR_FORCE_INLINE bool parse_true(const char* first, const char* last, bool* out) noexcept {
    if (last - first != 4)
        return false;

    const std::uint32_t lowered = load_u32(first) | 0x20202020U;
    if (lowered != CLASSIFY_SCALAR_TRUE_U32)
        return false;

    if (out)
        *out = true;

    return true;
}

CLASSIFY_SCALAR_FORCE_INLINE bool parse_false(const char* first, const char* last, bool* out) noexcept {
    if (last - first != 5)
        return false;

    const std::uint32_t lowered = load_u32(first) | 0x20202020U;
    if (lowered != CLASSIFY_SCALAR_FALSE_PREFIX_U32 || (static_cast<unsigned char>(first[4]) | 0x20U) != 'e')
        return false;

    if (out)
        *out = false;

    return true;
}

} // namespace parsing

namespace parsing_timestamp {

CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 bool is_leap_year(const int year) noexcept {
    return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

CLASSIFY_SCALAR_CONSTEXPR_VALUE_14 int common_days_in_month[13] = {
    0,
    31, 28, 31, 30, 31, 30,
    31, 31, 30, 31, 30, 31
};

CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 int days_in_month(const int year, const int month) noexcept {
    return month == 2 && is_leap_year(year) ? 29 : common_days_in_month[month];
}

template<std::size_t Count>
CLASSIFY_SCALAR_FORCE_INLINE bool parse_digits(const char* value, int& out) noexcept {
    int parsed = 0;
    for (std::size_t i = 0; i < Count; ++i) {
        const unsigned char c = static_cast<unsigned char>(value[i]);
        if (!ascii_digits[c])
            return false;

        parsed = (parsed * 10) + (value[i] - '0');
    }
    out = parsed;
    return true;
}

CLASSIFY_SCALAR_FORCE_INLINE bool valid_iso_date(const int year, const int month, const int day) noexcept {
    return (month < 1 || month > 12) ? false :
        day >= 1 && day <= days_in_month(year, month);
}

CLASSIFY_SCALAR_FORCE_INLINE std::int64_t days_from_civil(int year, const int month, const int day) noexcept {
    year -= month <= 2;
    const std::int64_t era = (year >= 0 ? year : year - 399) / 400;
    const unsigned yoe = static_cast<unsigned>(year - era * 400);
    const unsigned doy = (153U * static_cast<unsigned>(month + (month > 2 ? -3 : 9)) + 2U) / 5U
        + static_cast<unsigned>(day) - 1U;
    const unsigned doe = yoe * 365U + yoe / 4U - yoe / 100U + doy;
    return era * 146097 + static_cast<std::int64_t>(doe) - 719468;
}

CLASSIFY_SCALAR_FORCE_INLINE bool parse_iso_timestamp(
    const char* first,
    const char* last,
    std::uint64_t* out = nullptr) noexcept {
    if (last - first < 10)
        return false;

    if (first[4] != '-' || first[7] != '-')
        return false;

    int year = 0;
    int month = 0;
    int day = 0;
    if (!parse_digits<4>(first, year) || !parse_digits<2>(first + 5, month) || !parse_digits<2>(first + 8, day))
        return false;

    if (!valid_iso_date(year, month, day))
        return false;

    int hour = 0;
    int minute = 0;
    int second = 0;
    int millisecond = 0;
    int timezone_sign = 0;
    int timezone_hour = 0;
    int timezone_minute = 0;
    const char* current = first + 10;
    if (current != last) {
        if (ascii_lower_chars[static_cast<unsigned char>(*current)] != 't')
            return false;

        ++current;
        if (current + 5 > last || current[2] != ':')
            return false;

        if (!parse_digits<2>(current, hour) || !parse_digits<2>(current + 3, minute))
            return false;

        if (hour > 23 || minute > 59)
            return false;

        current += 5;
        if (current != last && *current == ':') {
            ++current;
            if (current + 2 > last)
                return false;

            if (!parse_digits<2>(current, second) || second > 59)
                return false;

            current += 2;
            if (current != last && *current == '.') {
                ++current;
                const char* fraction_first = current;
                while (current != last && ascii_digits[static_cast<unsigned char>(*current)]) {
                    if (current - fraction_first < 3)
                        millisecond = millisecond * 10 + (*current - '0');
                    ++current;
                }

                if (current == fraction_first)
                    return false;

                for (std::ptrdiff_t digits = current - fraction_first; digits < 3; ++digits)
                    millisecond *= 10;
            }
        }

        if (current != last) {
            if (ascii_lower_chars[static_cast<unsigned char>(*current)] == 'z') {
                ++current;
            } else {
                if (*current != '+' && *current != '-')
                    return false;

                timezone_sign = *current == '+' ? 1 : -1;
                if (current + 6 == last && current[3] == ':') {
                    if (!parse_digits<2>(current + 1, timezone_hour) || !parse_digits<2>(current + 4, timezone_minute))
                        return false;
                    current = last;
                } else if (current + 5 == last) {
                    if (!parse_digits<2>(current + 1, timezone_hour) || !parse_digits<2>(current + 3, timezone_minute))
                        return false;
                    current = last;
                } else {
                    return false;
                }

                if (timezone_hour > 23 || timezone_minute > 59)
                    return false;
            }
        }

        if (current != last)
            return false;
    }

    if (!out)
        return true;

    const std::int64_t timezone_offset_milliseconds =
        static_cast<std::int64_t>(timezone_sign)
        * (static_cast<std::int64_t>(timezone_hour) * 60 + timezone_minute)
        * 60 * 1000;
    const std::int64_t timestamp =
        days_from_civil(year, month, day) * 86400000
        + static_cast<std::int64_t>(hour) * 3600000
        + static_cast<std::int64_t>(minute) * 60000
        + static_cast<std::int64_t>(second) * 1000
        + millisecond
        - timezone_offset_milliseconds;

    if (timestamp < 0)
        return false;

    *out = static_cast<std::uint64_t>(timestamp);
    return true;
}

} // namespace parsing_timestamp

namespace parsing {

enum integer_parse_result {
    integer_parse_invalid,
    integer_parse_valid,
    integer_parse_overflow
};

CLASSIFY_SCALAR_FORCE_INLINE integer_parse_result finish_signed_integer(
    const parse_state& state,
    const std::uint64_t magnitude,
    const std::uint64_t limit,
    std::int64_t* out) noexcept {
    if (magnitude > limit)
        return integer_parse_overflow;

    if (out) {
        if (state.sign == parse_state::negative_sign) {
            *out = magnitude == limit
                ? int64_min_value
                : -static_cast<std::int64_t>(magnitude);
        } else {
            *out = static_cast<std::int64_t>(magnitude);
        }
    }

    return integer_parse_valid;
}

CLASSIFY_SCALAR_FORCE_INLINE integer_parse_result parse_integer_digits(
    const parse_state& state,
    const char* first,
    const char* last,
    const unsigned base,
    std::int64_t* out) noexcept {
    assert(first != last);

    const std::uint64_t limit = signed_integer_limits[state.sign];
    std::uint64_t acc = 0;
    for (const char* current = first; current != last; ++current) {
        const unsigned char digit = digit_values[static_cast<unsigned char>(*current)];
        if (digit >= base)
            return integer_parse_invalid;

        // Precomputing cutoff/cutlim looked cleaner but was measurably slower on MSVC.
        if (acc > (limit - digit) / base)
            return integer_parse_overflow;

        acc = (acc * base) + digit;
    }

    return finish_signed_integer(state, acc, limit, out);
}

CLASSIFY_SCALAR_FORCE_INLINE bool parse_hex_integer(
    const parse_state& state,
    std::int64_t* out) noexcept {
    const char* current = state.sign == parse_state::no_sign ? state.first : state.first + 1;
    const char* last = state.last;
    assert(current != last);

    if (current + 2 > last || current[0] != '0' || state.current != current + 1)
        return false;

    assert(current[1] == 'x' || current[1] == 'X');
    current += 2;

    assert(current != last);

    return parse_integer_digits(state, current, last, 16U, out) == integer_parse_valid;
}

CLASSIFY_SCALAR_FORCE_INLINE bool parse_bare_hex_integer(
    const parse_state& state,
    std::int64_t* out) noexcept {
    const char* current = state.sign == parse_state::no_sign ? state.first : state.first + 1;
    const char* last = state.last;
    assert(current != last);
    return parse_integer_digits(state, current, last, 16U, out) == integer_parse_valid;
}

CLASSIFY_SCALAR_FORCE_INLINE long double pow10_integer(const int exponent) noexcept {
    long double value = 1.0L;
    const long double factor = exponent >= 0 ? 10.0L : 0.1L;
    const int iterations = exponent >= 0 ? exponent : -exponent;
    for (int i = 0; i < iterations; ++i)
        value *= factor;

    return value;
}

template<char DecimalSymbol>
CLASSIFY_SCALAR_FORCE_INLINE bool parse_floating_ascii(
    const parse_state& state,
    double* out) noexcept {
    const char* current = state.numeric_first;
    const char* last = state.last;
    assert(current != last);

    if (state.sign == parse_state::negative_sign)
        ++current;

    long double parsed = 0.0L;
    bool has_digit = false;

    while (current != last && ascii_digits[static_cast<unsigned char>(*current)]) {
        parsed = (parsed * 10.0L) + static_cast<unsigned char>(*current - '0');
        has_digit = true;
        ++current;
    }

    if (current != last && static_cast<unsigned char>(*current) == static_cast<unsigned char>(DecimalSymbol)) {
        ++current;
        long double place = 0.1L;
        while (current != last && ascii_digits[static_cast<unsigned char>(*current)]) {
            parsed += static_cast<unsigned char>(*current - '0') * place;
            place *= 0.1L;
            has_digit = true;
            ++current;
        }
    }

    if (!has_digit)
        return false;

    if (current != last && (*current == 'e' || *current == 'E')) {
        ++current;
        if (current == last)
            return false;

        bool exponent_negative = false;
        const parse_state::Sign exponent_sign = parse_sign(static_cast<unsigned char>(*current));
        if (exponent_sign != parse_state::no_sign) {
            exponent_negative = exponent_sign == parse_state::negative_sign;
            ++current;
            if (current == last)
                return false;
        }

        int exponent = 0;
        while (current != last && ascii_digits[static_cast<unsigned char>(*current)]) {
            if (exponent > 500)
                return false;

            exponent = (exponent * 10) + (*current - '0');
            ++current;
        }

        if (current != last)
            return false;

        parsed *= pow10_integer(exponent_negative ? -exponent : exponent);
    }

    if (current != last)
        return false;

    if (state.sign == parse_state::negative_sign)
        parsed = -parsed;

    const double as_double = static_cast<double>(parsed);
    if (!std::isfinite(as_double))
        return false;

    if (out)
        *out = as_double;

    return true;
}

CLASSIFY_SCALAR_FORCE_INLINE bool parse_floating_dot(
    const parse_state& state,
    double* out) noexcept {
    const char* first = state.numeric_first;
    const char* last = state.last;
    const std::size_t size = static_cast<std::size_t>(last - first);
    assert(first != last);
    if (size > 4096)
        return false;

#ifdef CLASSIFY_SCALAR_HAS_STD_FLOAT_FROM_CHARS
    double parsed = 0;
    const std::from_chars_result result = std::from_chars(first, last, parsed);
    if (result.ec != std::errc() || result.ptr != last || !std::isfinite(parsed))
        return false;

    if (out)
        *out = parsed;

    return true;
#else
    return parse_floating_ascii<'.'>(state, out);
#endif
}

template<char DecimalSymbol>
CLASSIFY_SCALAR_FORCE_INLINE bool parse_floating_with_decimal(
    const parse_state& state,
    double* out) noexcept {
    const char* first = state.numeric_first;
    const char* last = state.last;
    const std::size_t size = static_cast<std::size_t>(last - first);
    assert(first != last);
    if (size > 4096)
        return false;

#ifdef CLASSIFY_SCALAR_HAS_STD_FLOAT_FROM_CHARS
    char buffer[4097];
    std::size_t i = 0;
    for (const char* current = first; current != last; ++current, ++i) {
        const unsigned char c = static_cast<unsigned char>(*current);
        if (is_ascii_space(static_cast<char>(c)))
            return false;
        if (DecimalSymbol != '.' && c == '.')
            return false;

        buffer[i] = c == static_cast<unsigned char>(DecimalSymbol) ? '.' : static_cast<char>(c);
    }
    buffer[size] = '\0';

    double parsed = 0;
    const std::from_chars_result result = std::from_chars(buffer, buffer + size, parsed);
    if (result.ec != std::errc() || result.ptr != buffer + size || !std::isfinite(parsed))
        return false;

    if (out)
        *out = parsed;

    return true;
#else
    return parse_floating_ascii<DecimalSymbol>(state, out);
#endif
}

template<char DecimalSymbol>
CLASSIFY_SCALAR_FORCE_INLINE bool parse_floating(
    const parse_state& state,
    double* out) noexcept {
    return DecimalSymbol == '.'
        ? parse_floating_dot(state, out)
        : parse_floating_with_decimal<DecimalSymbol>(state, out);
}

CLASSIFY_SCALAR_FORCE_INLINE bool floating_is_integral(const double value, std::int64_t* out) noexcept {
    if (value < int64_min_long_double || value > int64_max_long_double)
        return false;

    const std::int64_t integer = static_cast<std::int64_t>(value);
    // Exact round-trip test: we need to know whether the parsed double is
    // precisely representable as int64, not whether two measured floats are close.
    if (static_cast<double>(integer) != value)
        return false;

    if (out)
        *out = integer;

    return true;
}

template<typename Output>
CLASSIFY_SCALAR_FORCE_INLINE ScalarKind finish_integer(
    const std::int64_t parsed_integer,
    Output& output) noexcept {
    output.template set<scalar_int64>(parsed_integer);
    return integer::classify_integer_kind(parsed_integer);
}

CLASSIFY_SCALAR_FORCE_INLINE ScalarKind finish_floating(
    std::true_type,
    const double parsed_float,
    classify_only_output&) noexcept {
    std::int64_t parsed_integer = 0;
    if (floating_is_integral(parsed_float, &parsed_integer))
        return integer::classify_integer_kind(parsed_integer);

    return scalar_float;
}

template<typename Output>
CLASSIFY_SCALAR_FORCE_INLINE ScalarKind finish_floating(
    std::true_type,
    const double parsed_float,
    Output& output) noexcept {
    std::int64_t parsed_integer = 0;
    if (floating_is_integral(parsed_float, &parsed_integer))
        return finish_integer(parsed_integer, output);

    output.template set<scalar_float>(parsed_float);
    return scalar_float;
}

template<bool IntegralFloatingAsInteger, typename Output>
CLASSIFY_SCALAR_FORCE_INLINE ScalarKind finish_floating(
    const double parsed_float,
    Output& output) noexcept {
    IF_CONSTEXPR (IntegralFloatingAsInteger) {
        return finish_floating(std::true_type(), parsed_float, output);
    } else {
        output.template set<scalar_float>(parsed_float);
        return scalar_float;
    }
}

} // namespace parsing

template<char DecimalSymbol = '.', bool IntegralFloatingAsInteger = true>
struct builtin_numeric_policy {
    static_assert(DecimalSymbol != 'e' && DecimalSymbol != 'E', "decimal symbol cannot be an exponent marker");
    static_assert(DecimalSymbol != 'x' && DecimalSymbol != 'X', "decimal symbol cannot be a hexadecimal prefix marker");
    static_assert(DecimalSymbol < '0' || DecimalSymbol > '9', "decimal symbol cannot be an ASCII digit");

    CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 static bool matches_leading(unsigned char c) noexcept {
        return (c >= '0' && c <= '9') || c == static_cast<unsigned char>(DecimalSymbol) || c == '+' || c == '-';
    }

    template<typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE ScalarKind on_dispatch(
        parse_state& state,
        Output& output) const noexcept {
        return on_number(state, output);
    }

    template<typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE ScalarKind on_decimal(
        parse_state& state,
        Output& output) const noexcept {
        double parsed_float = 0;
        return parsing::parse_floating<DecimalSymbol>(state, &parsed_float)
            ? parsing::finish_floating<IntegralFloatingAsInteger>(parsed_float, output)
            : scalar_string;
    }

    template<typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE ScalarKind scan_short_number(
        parse_state& state,
        const char* value_first,
        Output& output) const noexcept {
        std::uint64_t acc = 0;

        for (const char* current = value_first; current != state.last; ++current) {
            state.current = current;
            const unsigned char c = static_cast<unsigned char>(*current);

            const ParseFlag flag = parse_table<DecimalSymbol>()[c];
            switch (flag) {
            case ParseFlag::digit:
                acc = (acc * 10U) + parsing::decimal_digit_value(c);
                continue;
            case ParseFlag::decimal:
                return on_decimal(state, output);
            case ParseFlag::other:
            default:
                if (ascii_lower_chars[c] == 'e')
                    return on_decimal(state, output);
                return scalar_string;
            }
        }

        state.current = state.last;
        std::int64_t parsed_integer = state.sign == parse_state::negative_sign
            ? -static_cast<std::int64_t>(acc)
            : static_cast<std::int64_t>(acc);
        return parsing::finish_integer(parsed_integer, output);
    }

    template<typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE ScalarKind scan_checked_number(
        parse_state& state,
        const char* value_first,
        Output& output) const noexcept {
        std::uint64_t acc = 0;
        const std::uint64_t limit = signed_integer_limits[state.sign];
        bool overflow = false;

        for (const char* current = value_first; current != state.last; ++current) {
            state.current = current;
            const unsigned char c = static_cast<unsigned char>(*current);

            const ParseFlag flag = parse_table<DecimalSymbol>()[c];
            switch (flag) {
            case ParseFlag::digit: {
                const unsigned char digit = parsing::decimal_digit_value(c);
                if (!overflow) {
                    // Precomputing cutoff/cutlim, digit-count gating, and cold overflow helpers all benchmarked slower.
                    if (acc > (limit - digit) / 10U)
                        overflow = true;
                    else
                        acc = (acc * 10U) + digit;
                }
                continue;
            }
            case ParseFlag::decimal:
                return on_decimal(state, output);
            case ParseFlag::other:
            default:
                if (ascii_lower_chars[c] == 'e')
                    return on_decimal(state, output);
                return scalar_string;
            }
        }

        state.current = state.last;
        if (overflow)
            return scalar_bigint;

        std::int64_t parsed_integer = 0;
        parsing::finish_signed_integer(state, acc, limit, &parsed_integer);
        return parsing::finish_integer(parsed_integer, output);
    }

    template<typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE ScalarKind scan_number(
        parse_state& state,
        const char* value_first,
        Output& output) const noexcept {
		// Use overflow checks for numbers with 19 or more digits, which can exceed 64-bit limits.
        // Shorter numbers are common enough that it's worth skipping the checks for them.
        // Testing note: yes this was a significant optimization.
        return state.last - value_first < 19
            ? scan_short_number(state, value_first, output)
            : scan_checked_number(state, value_first, output);
    }

    template<typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE ScalarKind on_number(
        parse_state& state,
        Output& output) const noexcept {
        const char* value_first = parsing::apply_leading_sign(state);
        if (value_first == state.last)
            return scalar_string;

        const unsigned char value_first_char = static_cast<unsigned char>(*value_first);
        if (!ascii_digits[value_first_char] && value_first_char != static_cast<unsigned char>(DecimalSymbol))
            return scalar_string;

        if (value_first_char == '0' && value_first + 1 != state.last) {
            const unsigned char second_char = static_cast<unsigned char>(value_first[1]);
            if (ascii_lower_chars[second_char] == 'x') {
                if (value_first + 2 == state.last)
                    return scalar_string;

                state.current = value_first + 1;
                std::int64_t parsed_integer = 0;
                return parsing::parse_hex_integer(state, &parsed_integer)
                    ? parsing::finish_integer(parsed_integer, output)
                    : scalar_string;
            }
        }

        return scan_number(state, value_first, output);
    }
};

struct builtin_bool_policy {
    CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 static bool matches_leading(unsigned char c) noexcept {
        return c == 't' || c == 'T' || c == 'f' || c == 'F';
    }

    template<typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE ScalarKind on_dispatch(
        parse_state& state,
        Output& output) const noexcept {
        bool parsed = false;
        if (!(parsing::parse_true(state.first, state.last, &parsed) || parsing::parse_false(state.first, state.last, &parsed)))
            return scalar_string;

        output.template set<scalar_bool>(parsed);
        return scalar_bool;
    }
};

struct builtin_timestamp_policy {
    CLASSIFY_SCALAR_CONST CLASSIFY_SCALAR_CONSTEXPR_14 static bool matches_leading(unsigned char c) noexcept {
        return c >= '0' && c <= '9';
    }

    template<typename Output>
    CLASSIFY_SCALAR_FORCE_INLINE ScalarKind on_dispatch(
        parse_state& state,
        Output& output) const noexcept {
        std::uint64_t timestamp = 0;
        if (!parsing_timestamp::parse_iso_timestamp(state.first, state.last, &timestamp))
            return scalar_string;

        output.template set<scalar_timestamp>(timestamp);
        return scalar_timestamp;
    }
};

template<ScalarKind Kind>
struct scalar_home;

template<typename T>
struct signed_integer_scalar_home {
    typedef T type;
    typedef policy_pack<builtin_numeric_policy<> > policy;

    static T get(long double, std::int64_t integer, std::uint64_t, bool) noexcept {
        return static_cast<T>(integer);
    }
};

template<>
struct scalar_home<scalar_bool> {
    typedef bool type;
    typedef policy_pack<builtin_bool_policy> policy;

    static bool get(long double, std::int64_t, std::uint64_t, bool boolean) noexcept {
        return boolean;
    }
};

template<>
struct scalar_home<scalar_int8> : signed_integer_scalar_home<std::int8_t> {};

template<>
struct scalar_home<scalar_int16> : signed_integer_scalar_home<std::int16_t> {};

template<>
struct scalar_home<scalar_int32> : signed_integer_scalar_home<std::int32_t> {};

template<>
struct scalar_home<scalar_int64> : signed_integer_scalar_home<std::int64_t> {};

template<>
struct scalar_home<scalar_float> {
    typedef double type;
    typedef policy_pack<builtin_numeric_policy<> > policy;

    static double get(long double number, std::int64_t, std::uint64_t, bool) noexcept {
        return static_cast<double>(number);
    }
};

template<>
struct scalar_home<scalar_timestamp> {
    typedef std::uint64_t type;
    typedef policy_pack<builtin_timestamp_policy> policy;

    static std::uint64_t get(long double, std::int64_t, std::uint64_t timestamp, bool) noexcept {
        return timestamp;
    }
};

template<char DecimalSymbol, bool TrimAsciiWhitespace>
CLASSIFY_SCALAR_FORCE_INLINE bool parse_float_with_decimal(
    const char* first,
    const char* last,
    double& out) noexcept {
    const scalar_span span = parsing::trim_span<TrimAsciiWhitespace>(first, last);
    if (span.first == span.last)
        return false;

    parse_state state(span.first, span.last);
    const char* value_first = parsing::apply_leading_sign(state);
    if (value_first == state.last)
        return false;

    return parsing::parse_floating<DecimalSymbol>(state, &out);
}

} // namespace detail

typedef detail::parse_state parse_state;

/// Ordered set of scalar policies; earlier policies have higher priority.
template<typename... Policies>
using policy_pack = detail::policy_pack<Policies...>;

/// Built-in numeric recognizer for int, float, bigint, and 0x-prefixed hex.
template<char DecimalSymbol = '.', bool IntegralFloatingAsInteger = true>
using builtin_numeric_policy = detail::builtin_numeric_policy<DecimalSymbol, IntegralFloatingAsInteger>;

/// Built-in case-insensitive true/false recognizer.
typedef detail::builtin_bool_policy builtin_bool_policy;

/// Built-in conservative ISO date/date-time recognizer.
typedef detail::builtin_timestamp_policy builtin_timestamp_policy;

/// Default policy pack: numeric, timestamp, then bool.
typedef detail::policy_pack<
    detail::builtin_numeric_policy<>,
    detail::builtin_timestamp_policy,
    detail::builtin_bool_policy> builtin_policy_pack;

/// Alias for the default built-in policy pack.
typedef builtin_policy_pack default_policy_pack;

/// Numeric and bool policy pack with timestamp recognition disabled.
typedef detail::policy_pack<
    detail::builtin_numeric_policy<>,
    detail::builtin_bool_policy> numeric_bool_policy_pack;

/// Numeric-only policy pack for null/string/int/float/bigint inference.
typedef detail::policy_pack<
    detail::builtin_numeric_policy<> > numeric_policy_pack;

/// Classify a pointer span using the selected policy pack and output adapter.
template<
    typename Kind = ScalarKind,
    bool TrimAsciiWhitespace = true,
    typename Output = classify_only_output,
    typename Policy = default_policy_pack>
CLASSIFY_SCALAR_FORCE_INLINE Kind classify_scalar(
    const char* first,
    const char* last,
    Output output = Output(),
    Policy policy = Policy()) noexcept {
    const scalar_span span = detail::parsing::trim_span<TrimAsciiWhitespace>(first, last);
    if (span.first == span.last)
        return !first || !last || last < first
            ? detail::scalar_kind_cast<Kind>(scalar_string)
            : detail::scalar_kind_cast<Kind>(scalar_null);

    detail::parse_state state(span.first, span.last);
    return policy.template dispatch<Kind>(
        detail::dispatch_table<Policy>()[static_cast<unsigned char>(*span.first)],
        state,
        output);
}

/// Classify a string literal using the selected policy pack and output adapter.
template<
    typename Kind = ScalarKind,
    bool TrimAsciiWhitespace = true,
    typename Output = classify_only_output,
    typename Policy = default_policy_pack,
    std::size_t N>
CLASSIFY_SCALAR_FORCE_INLINE Kind classify_scalar(
    const char (&value)[N],
    Output output = Output(),
    Policy policy = Policy()) noexcept {
    return classify_scalar<Kind, TrimAsciiWhitespace>(
        value,
        value + (N ? N - 1 : 0),
        output,
        policy);
}

#ifdef CLASSIFY_SCALAR_HAS_CXX17
/// Classify a string_view using the selected policy pack and output adapter.
template<
    typename Kind = ScalarKind,
    bool TrimAsciiWhitespace = true,
    typename Output = classify_only_output,
    typename Policy = default_policy_pack>
CLASSIFY_SCALAR_FORCE_INLINE Kind classify_scalar(
    std::string_view value,
    Output output = Output(),
    Policy policy = Policy()) noexcept {
    if (value.empty())
        return detail::scalar_kind_cast<Kind>(scalar_null);

    return classify_scalar<Kind, TrimAsciiWhitespace>(
        value.data(),
        value.data() + value.size(),
        output,
        policy);
}

#endif

/// Parse an explicit hexadecimal integer, accepting either bare hex or 0x-prefixed hex.
template<bool TrimAsciiWhitespace = true, typename IntegerType>
CLASSIFY_SCALAR_FORCE_INLINE typename std::enable_if<
    std::is_integral<IntegerType>::value
        && !std::is_same<IntegerType, bool>::value,
    bool>::type parse_hex(
    const char* first,
    const char* last,
    IntegerType& out) noexcept {
    const scalar_span span = detail::parsing::trim_span<TrimAsciiWhitespace>(first, last);
    if (span.first == span.last)
        return false;

    detail::parse_state state(span.first, span.last);
    const char* current = detail::parsing::apply_leading_sign(state);
    if (current == state.last)
        return false;

    std::int64_t parsed = 0;
    if (current + 2 <= state.last && current[0] == '0' && (current[1] == 'x' || current[1] == 'X')) {
        state.current = current + 1;
        if (!detail::parsing::parse_hex_integer(state, &parsed))
            return false;
    } else if (!detail::parsing::parse_bare_hex_integer(state, &parsed))
        return false;

    if (parsed < static_cast<std::int64_t>(std::numeric_limits<IntegerType>::min()))
        return false;

    if (parsed >= 0
            && static_cast<std::uint64_t>(parsed) > static_cast<std::uint64_t>(std::numeric_limits<IntegerType>::max()))
        return false;

    out = static_cast<IntegerType>(parsed);
    return true;
}

/// String-literal overload for parse_hex().
template<bool TrimAsciiWhitespace = true, std::size_t Size, typename IntegerType>
CLASSIFY_SCALAR_FORCE_INLINE typename std::enable_if<
    std::is_integral<IntegerType>::value
        && !std::is_same<IntegerType, bool>::value,
    bool>::type parse_hex(
    const char (&value)[Size],
    IntegerType& out) noexcept {
    return parse_hex<TrimAsciiWhitespace>(value, value + Size - 1, out);
}

/// Parse an explicit floating-point value with runtime '.' or ',' decimal selection.
template<bool TrimAsciiWhitespace = true>
CLASSIFY_SCALAR_FORCE_INLINE bool parse_float(
    const char* first,
    const char* last,
    double& out,
    const char decimal_symbol = '.') noexcept {
    switch (decimal_symbol) {
    case '.':
        return detail::parse_float_with_decimal<'.', TrimAsciiWhitespace>(first, last, out);
    case ',':
        return detail::parse_float_with_decimal<',', TrimAsciiWhitespace>(first, last, out);
    default:
        return false;
    }
}

/// Parse one built-in scalar kind directly and store its natural C++ value.
template<ScalarKind Kind, bool TrimAsciiWhitespace = true>
CLASSIFY_SCALAR_FORCE_INLINE bool parse_scalar(
    const char* first,
    const char* last,
    typename detail::scalar_home<Kind>::type& out) noexcept {
    long double number = 0;
    std::int64_t integer = 0;
    std::uint64_t timestamp = 0;
    bool boolean = false;

    const ScalarKind kind = classify_scalar<ScalarKind, TrimAsciiWhitespace>(
        first,
        last,
        builtin_output_refs(number, integer, boolean, timestamp),
        typename detail::scalar_home<Kind>::policy());
    if (kind != Kind
            && !(Kind == scalar_float && detail::integer::is_signed_integer_kind(kind))
            && !detail::integer::integer_kind_fits_in(kind, Kind))
        return false;

    out = detail::scalar_home<Kind>::get(number, integer, timestamp, boolean);
    return true;
}

/// Parse a signed integer directly into the requested C++ integer type.
template<typename IntegerType, bool TrimAsciiWhitespace = true>
CLASSIFY_SCALAR_FORCE_INLINE typename std::enable_if<
    std::is_integral<IntegerType>::value
        && std::is_signed<IntegerType>::value
        && !std::is_same<IntegerType, bool>::value,
    bool>::type parse_scalar(
    const char* first,
    const char* last,
    IntegerType& out) noexcept {
    long double number = 0;
    std::int64_t integer = 0;
    bool boolean = false;

    const ScalarKind kind = classify_scalar<ScalarKind, TrimAsciiWhitespace>(
        first,
        last,
        output_refs(number, integer, boolean),
        numeric_policy_pack());
    if (!detail::integer::is_signed_integer_kind(kind))
        return false;

    if (integer < static_cast<std::int64_t>(std::numeric_limits<IntegerType>::min())
            || integer > static_cast<std::int64_t>(std::numeric_limits<IntegerType>::max()))
        return false;

    out = static_cast<IntegerType>(integer);
    return true;
}

#ifdef CLASSIFY_SCALAR_HAS_CXX17
/// string_view overload for parse_hex().
template<bool TrimAsciiWhitespace = true, typename IntegerType>
CLASSIFY_SCALAR_FORCE_INLINE typename std::enable_if<
    std::is_integral<IntegerType>::value
        && !std::is_same<IntegerType, bool>::value,
    bool>::type parse_hex(
    std::string_view value,
    IntegerType& out) noexcept {
    return parse_hex<TrimAsciiWhitespace>(value.data(), value.data() + value.size(), out);
}

/// string_view overload for parse_float().
template<bool TrimAsciiWhitespace = true>
CLASSIFY_SCALAR_FORCE_INLINE bool parse_float(
    std::string_view value,
    double& out,
    const char decimal_symbol = '.') noexcept {
    return parse_float<TrimAsciiWhitespace>(value.data(), value.data() + value.size(), out, decimal_symbol);
}

/// string_view overload for parse_scalar().
template<ScalarKind Kind, bool TrimAsciiWhitespace = true>
CLASSIFY_SCALAR_FORCE_INLINE bool parse_scalar(
    std::string_view value,
    typename detail::scalar_home<Kind>::type& out) noexcept {
    return parse_scalar<Kind, TrimAsciiWhitespace>(value.data(), value.data() + value.size(), out);
}

/// string_view overload for signed integer parse_scalar<T>().
template<typename IntegerType, bool TrimAsciiWhitespace = true>
CLASSIFY_SCALAR_FORCE_INLINE typename std::enable_if<
    std::is_integral<IntegerType>::value
        && std::is_signed<IntegerType>::value
        && !std::is_same<IntegerType, bool>::value,
    bool>::type parse_scalar(
    std::string_view value,
    IntegerType& out) noexcept {
    return parse_scalar<IntegerType, TrimAsciiWhitespace>(value.data(), value.data() + value.size(), out);
}
#endif

} // namespace classify_scalar

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

#endif // CLASSIFY_SCALAR_SKIP_HEADER
