/** @file
 *  @brief CSV scalar type classification adapter.
 */

#pragma once

#include <cstdint>

#include "common.hpp"
#include "../external/classify_scalar.hpp"

namespace csv {
    /** Enumerates the different CSV field types recognized by this library. */
    enum class DataType {
        UNKNOWN = classify_scalar::scalar_invalid,
        CSV_NULL = classify_scalar::scalar_null,           /**< Empty string */
        CSV_STRING = classify_scalar::scalar_string,       /**< Non-scalar string */
        CSV_BOOL = classify_scalar::scalar_bool,           /**< Boolean value */
        CSV_INT8 = classify_scalar::scalar_int8,           /**< 8-bit integer */
        CSV_INT16 = classify_scalar::scalar_int16,         /**< 16-bit integer */
        CSV_INT32 = classify_scalar::scalar_int32,         /**< 32-bit integer */
        CSV_INT64 = classify_scalar::scalar_int64,         /**< 64-bit integer */
        CSV_BIGINT = classify_scalar::scalar_bigint,       /**< Integer too large to fit in 64 bits */
        CSV_DOUBLE = classify_scalar::scalar_float,        /**< Floating point value */
        CSV_TIMESTAMP = classify_scalar::scalar_timestamp, /**< Timestamp value */
        scalar_custom_begin = classify_scalar::scalar_custom_begin - 1
    };

    static_assert(DataType::CSV_STRING < DataType::CSV_INT8, "String type should come before numeric types.");
    static_assert(DataType::CSV_INT8 < DataType::CSV_INT64, "Smaller integer types should come before larger integer types.");
    static_assert(DataType::CSV_INT64 < DataType::CSV_DOUBLE, "Integer types should come before floating point value types.");

    namespace internals {
        /** Cached scalar classification and parsed value for one CSV field. */
        struct CSVFieldScalar {
            DataType type = DataType::UNKNOWN;
            std::int64_t integer = 0;
            long double floating = 0;
            std::uint64_t timestamp = 0;
            bool boolean = false;
        };

        struct CSVFieldScalarOutput {
            CSVFieldScalar& value;

            template<classify_scalar::ScalarKind Kind>
            typename std::enable_if<
                Kind == classify_scalar::scalar_int8
                || Kind == classify_scalar::scalar_int16
                || Kind == classify_scalar::scalar_int32
                || Kind == classify_scalar::scalar_int64,
                void>::type set(std::int64_t parsed) const noexcept {
                value.integer = parsed;
            }

            template<classify_scalar::ScalarKind Kind>
            typename std::enable_if<Kind == classify_scalar::scalar_float, void>::type set(long double parsed) const noexcept {
                value.floating = parsed;
            }

            template<classify_scalar::ScalarKind Kind>
            typename std::enable_if<Kind == classify_scalar::scalar_bool, void>::type set(bool parsed) const noexcept {
                value.boolean = parsed;
            }

            template<classify_scalar::ScalarKind Kind>
            typename std::enable_if<Kind == classify_scalar::scalar_timestamp, void>::type set(std::uint64_t parsed) const noexcept {
                value.timestamp = parsed;
            }
        };

        inline CSVFieldScalar classify_field_scalar(csv::string_view in) {
            CSVFieldScalar scalar;
            if (in.empty()) {
                scalar.type = DataType::CSV_NULL;
                return scalar;
            }

            const char* first = in.data();
            const char* last = first + in.size();
            typedef classify_scalar::policy_pack<
                classify_scalar::builtin_numeric_policy<'.', false>,
                classify_scalar::builtin_timestamp_policy,
                classify_scalar::builtin_bool_policy
            > csv_field_policy_pack;

            scalar.type = classify_scalar::classify_scalar<
                DataType,
                true>(first, last, CSVFieldScalarOutput{ scalar }, csv_field_policy_pack());
            return scalar;
        }

#ifndef DOXYGEN_SHOULD_SKIP_THIS
        inline DataType data_type(csv::string_view in);
#endif

        /** Classify values using the CSVField scalar policy without materializing parsed output. */
        inline DataType data_type(csv::string_view in) {
            if (in.empty())
                return DataType::CSV_NULL;

            const char* first = in.data();
            const char* last = first + in.size();
            typedef classify_scalar::policy_pack<
                classify_scalar::builtin_numeric_policy<'.', false>,
                classify_scalar::builtin_timestamp_policy,
                classify_scalar::builtin_bool_policy
            > csv_field_policy_pack;

            return classify_scalar::classify_scalar<
                DataType,
                true>(first, last, classify_scalar::classify_only_output(), csv_field_policy_pack());
        }
    }
}
