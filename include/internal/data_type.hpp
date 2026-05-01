/** @file
 *  @brief CSV scalar type classification adapter.
 */

#pragma once

#include <cstdint>

#include "common.hpp"
CSV_MSVC_PUSH_DISABLE(4127)
#include "../external/classify_scalar.hpp"
CSV_MSVC_POP

namespace csv {
    /** Enumerates the different CSV field types recognized by this library. */
    enum class DataType {
        UNKNOWN = -1,
        CSV_NULL,      /**< Empty string */
        CSV_STRING,    /**< Non-scalar string */
        CSV_INT8,      /**< 8-bit integer */
        CSV_INT16,     /**< 16-bit integer */
        CSV_INT32,     /**< 32-bit integer */
        CSV_INT64,     /**< 64-bit integer */
        CSV_BIGINT,    /**< Integer too large to fit in 64 bits */
        CSV_DOUBLE,    /**< Floating point value */
        CSV_BOOL,      /**< Boolean value */
        CSV_TIMESTAMP  /**< Timestamp value */
    };

    static_assert(DataType::CSV_STRING < DataType::CSV_INT8, "String type should come before numeric types.");
    static_assert(DataType::CSV_INT8 < DataType::CSV_INT64, "Smaller integer types should come before larger integer types.");
    static_assert(DataType::CSV_INT64 < DataType::CSV_DOUBLE, "Integer types should come before floating point value types.");

    namespace internals {
#ifndef DOXYGEN_SHOULD_SKIP_THIS
        inline DataType data_type(csv::string_view in, long double* const out = nullptr);
#endif

        static_assert(classify_scalar::integer_none == 0, "IntegerKind table assumes integer_none is 0.");
        static_assert(classify_scalar::integer_int8 == 1, "IntegerKind table assumes integer_int8 is 1.");
        static_assert(classify_scalar::integer_int16 == 2, "IntegerKind table assumes integer_int16 is 2.");
        static_assert(classify_scalar::integer_int32 == 3, "IntegerKind table assumes integer_int32 is 3.");
        static_assert(classify_scalar::integer_int64 == 4, "IntegerKind table assumes integer_int64 is 4.");
        CSV_PRIVATE CONSTEXPR_VALUE_14 DataType integer_kind_data_types[] = {
            DataType::CSV_STRING,
            DataType::CSV_INT8,
            DataType::CSV_INT16,
            DataType::CSV_INT32,
            DataType::CSV_INT64
        };

        static_assert(classify_scalar::scalar_null == 0, "ScalarKind table assumes scalar_null is 0.");
        static_assert(classify_scalar::scalar_string == 1, "ScalarKind table assumes scalar_string is 1.");
        static_assert(classify_scalar::scalar_bool == 2, "ScalarKind table assumes scalar_bool is 2.");
        static_assert(classify_scalar::scalar_int == 3, "ScalarKind table assumes scalar_int is 3.");
        static_assert(classify_scalar::scalar_float == 4, "ScalarKind table assumes scalar_float is 4.");
        static_assert(classify_scalar::scalar_timestamp == 5, "ScalarKind table assumes scalar_timestamp is 5.");
        static_assert(classify_scalar::scalar_bigint == 6, "ScalarKind table assumes scalar_bigint is 6.");
        CSV_PRIVATE CONSTEXPR_VALUE_14 DataType scalar_kind_data_types[] = {
            DataType::CSV_NULL,
            DataType::CSV_STRING,
            DataType::CSV_BOOL,
            DataType::CSV_STRING,
            DataType::CSV_DOUBLE,
            DataType::CSV_TIMESTAMP,
            DataType::CSV_BIGINT
        };

        CSV_PRIVATE CONSTEXPR_14 DataType data_type_from_integer_kind(
            classify_scalar::IntegerKind kind) noexcept {
            return kind <= classify_scalar::integer_int64
                ? integer_kind_data_types[static_cast<unsigned char>(kind)]
                : DataType::CSV_STRING;
        }

        CSV_PRIVATE CONSTEXPR_14 DataType data_type_from_scalar_kind(
            classify_scalar::ScalarKind kind,
            classify_scalar::IntegerKind integer_kind = classify_scalar::integer_none) noexcept {
            return kind == classify_scalar::scalar_int
                ? data_type_from_integer_kind(integer_kind)
                : (kind >= classify_scalar::scalar_null && kind <= classify_scalar::scalar_bigint
                    ? scalar_kind_data_types[static_cast<int>(kind)]
                    : DataType::CSV_STRING);
        }

        /** Classify null/string/int/bigint/double values for the legacy CSVField API. */
        inline DataType data_type(csv::string_view in, long double* const out) {
            std::int64_t integer = 0;
            long double number = 0;
            bool boolean = false;
            classify_scalar::IntegerKind integer_kind = classify_scalar::integer_none;
            classify_scalar::builtin_output_refs output =
                classify_scalar::output_refs(number, integer, boolean, integer_kind);

            const char* first = in.data();
            const char* last = first + in.size();
            typedef classify_scalar::policy_pack<
                classify_scalar::builtin_numeric_policy<'.', false>
            > csv_numeric_policy_pack;

            CSV_MSVC_PUSH_DISABLE(4127)
            const classify_scalar::ScalarKind kind = classify_scalar::classify_scalar<
                classify_scalar::ScalarKind,
                true>(first, last, output, csv_numeric_policy_pack());
            CSV_MSVC_POP

            switch (kind) {
            case classify_scalar::scalar_int:
                if (out)
                    *out = static_cast<long double>(integer);
                break;
            case classify_scalar::scalar_float:
                if (out)
                    *out = number;
                break;
            default:
                break;
            }

            return data_type_from_scalar_kind(kind, integer_kind);
        }
    }
}
