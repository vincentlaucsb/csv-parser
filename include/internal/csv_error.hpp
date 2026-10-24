/** @file
 *  @brief Explicit error reporting shared by CSV reading, writing and DataFrames.
 */
#pragma once
#include "common.hpp"
#include <string>
#include <system_error>
#include <utility>

namespace csv {
    /**
    * @enum CSVConversionError
    * @brief Non-throwing CSVField conversion result.
    *
    * Returned by CSVField::as() inside std::expected, and used internally by
    * CSVField::get() and CSVField::try_get() to keep throwing and non-throwing
    * conversions on the same rules.
    *
    * @sa csv_conversion_error_message()
    */
    enum class CSVConversionError {
        /** Conversion succeeded. */
        None = 0,

        /** The field is not compatible with the requested target type. */
        NotANumber,

        /** The parsed value does not fit in the requested target type. */
        Overflow,

        /** A floating point field was requested as an integral type. */
        FloatToInt,

        /** A negative value was requested as an unsigned type. */
        NegativeToUnsigned
    };

    namespace internals {
        typedef const char* csv_error_message;

        static CONSTEXPR_VALUE_14 csv_error_message CSV_CONVERSION_ERROR_MESSAGES[] = {
            "",
            "Not a number.",
            "Overflow error.",
            "Attempted to convert a floating point value to an integral type.",
            "Negative numbers cannot be converted to unsigned types."
        };
    }

    /** Return a stable human-readable description for a CSVConversionError. */
    inline const char* csv_conversion_error_message(CSVConversionError error) noexcept {
        const size_t index = static_cast<size_t>(error);
        return index < (sizeof(internals::CSV_CONVERSION_ERROR_MESSAGES) / sizeof(internals::CSV_CONVERSION_ERROR_MESSAGES[0]))
            ? internals::CSV_CONVERSION_ERROR_MESSAGES[index]
            : internals::CSV_CONVERSION_ERROR_MESSAGES[static_cast<size_t>(CSVConversionError::NotANumber)];
    }

    /** Operational failures. Conversion failures retain their CSVConversionError cause. */
    enum class CSVErrorCode {
        None, InvalidArgument, InvalidFormat, InvalidChunkSize,
        OpenFailed, MappingFailed, ReadFailed, WriteFailed, UnsupportedEncoding,
        RowTooLarge, RowLengthMismatch, ColumnNotFound, IndexOutOfBounds,
        ConversionFailed, MissingKeyColumn, KeyNotFound, DuplicateKey,
        UnkeyedFrame, ReadOnlyProxy, InvalidProxy, KeyColumnProtected,
        UnsupportedOperation, SizeLimitExceeded
    };

    /** An owned diagnostic; success and EOF clear the error.
     *  Explicit errors cover library validation and I/O, not allocation failures
     *  or exceptions thrown by user callbacks. Partial progress is not rolled back.
     */
    struct CSVError {
        CSVErrorCode code = CSVErrorCode::None;
        CSVConversionError conversion = CSVConversionError::None;
        std::error_code system;
        std::string context;

        CSVError() = default;
        CSVError(CSVErrorCode value, std::string message)
            : code(value), context(std::move(message)) {}
        explicit operator bool() const noexcept { return code != CSVErrorCode::None; }
        void clear() { *this = CSVError(); }
        std::string message() const {
            return system ? context + ": " + system.message() : context;
        }
    };

    namespace internals {
        inline bool fail(CSVError& error, CSVErrorCode code, std::string message) {
            error = CSVError(code, std::move(message));
            return false;
        }
    }
}
