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
        None,                /**< Success, including end-of-stream without a row. */
        InvalidArgument,     /**< Invalid operation input, such as a null stream or zero batch size. */
        InvalidFormat,       /**< Conflicting parsing characters or an unresolved delimiter query. */
        InvalidChunkSize,    /**< Configured read-window size is outside the supported byte limits. */
        OpenFailed,          /**< A native file could not be opened, including initial input mapping. */
        MappingFailed,       /**< A subsequent input-file region could not be memory-mapped; see CSVError::system. */
        ReadFailed,          /**< The input stream reported an I/O failure rather than ordinary EOF. */
        WriteFailed,         /**< Writing, flushing, or closing the output failed. */
        UnsupportedEncoding, /**< A UTF-16/UTF-32 BOM was found; transcode the input to UTF-8 first. */
        RowTooLarge,         /**< A complete record could not fit within the configured read window. */
        RowLengthMismatch,   /**< A record's field count violates the configured variable-column policy. */
        ColumnNotFound,      /**< A requested name is absent from the visible column schema. */
        IndexOutOfBounds,    /**< A row/column position exceeds the visible schema or physical record width. */
        ConversionFailed,   /**< A typed field/key conversion failed; see CSVError::conversion for its exact cause. */
        MissingKeyColumn,    /**< Keyed construction has an empty key-column name or a name absent from the schema. */
        KeyNotFound,         /**< A requested row key is absent from the frame's key index. */
        DuplicateKey,        /**< A key conflicts with an existing row under the requested insertion/duplicate policy. */
        UnkeyedFrame,        /**< A key-based operation was requested on a positional-only frame. */
        ReadOnlyProxy,       /**< Mutation was requested through a cell, row, or column without edit permission. */
        InvalidProxy,        /**< A proxy is unbound or a column proxy was invalidated by structural mutation. */
        KeyColumnProtected,  /**< Removing the column that supplies the frame's stored keys is prohibited. */
        UnsupportedOperation, /**< The operation cannot be performed for this frame, such as inserting into a custom-key frame. */
        SizeLimitExceeded    /**< Combined preserved string-view key bytes exceed std::string::max_size(). */
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
