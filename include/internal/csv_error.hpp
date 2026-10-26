/** @file
 *  @brief Explicit error reporting shared by CSV reading, writing and DataFrames.
 */
#pragma once
#include "common.hpp"
#include <cstring>
#include <memory>
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

    /** Operational failures reported through CSVError. */
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
        ConversionFailed,   /**< A DataFrame field/key conversion failed; context describes the cause. */
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

    /** A diagnostic whose text remains valid for the lifetime of the error.
     *  Static messages are borrowed; dynamic messages are owned. Copies preserve
     *  that lifetime, and moved-from errors are cleared. Success and EOF clear
     *  the error without allocating.
     *  Explicit errors cover library validation and I/O, not allocation failures
     *  or exceptions thrown by user callbacks. Partial progress is not rolled back.
     */
    struct CSVError {
        CSVErrorCode code = CSVErrorCode::None;
        std::error_code system;
        /** Diagnostic text; use set_context() to replace it with an owned copy. */
        csv::string_view context;

        CSVError() = default;
        /** Copy runtime text, including temporary strings and exception messages. */
        CSVError(CSVErrorCode value, csv::string_view message) : code(value) {
            this->set_context(message);
        }
        CSVError(const CSVError& other)
            : code(other.code), system(other.system), context(other.context) {
            if (other.owned_context_) this->set_context(other.context);
        }
        CSVError(CSVError&& other) noexcept
            : code(other.code), system(other.system), context(other.context),
              owned_context_(std::move(other.owned_context_)) {
            other.clear();
        }
        CSVError& operator=(const CSVError& other) {
            if (this != &other) {
                CSVError copy(other);
                *this = std::move(copy);
            }
            return *this;
        }
        CSVError& operator=(CSVError&& other) noexcept {
            if (this != &other) {
                code = other.code;
                system = other.system;
                context = other.context;
                owned_context_ = std::move(other.owned_context_);
                other.clear();
            }
            return *this;
        }

        /** Borrow text that must remain valid for the program's lifetime. */
        static CSVError from_static(CSVErrorCode value, csv::string_view message) noexcept {
            CSVError error;
            error.code = value;
            error.context = message;
            return error;
        }

        /** Replace the context with an owned copy; accepts views into this error. */
        void set_context(csv::string_view message) {
            std::unique_ptr<char[]> buffer;
            if (!message.empty()) {
                buffer.reset(new char[message.size()]);
                std::memcpy(buffer.get(), message.data(), message.size());
            }
            context = message.empty() ? csv::string_view()
                : csv::string_view(buffer.get(), message.size());
            owned_context_ = std::move(buffer);
        }
        explicit operator bool() const noexcept { return code != CSVErrorCode::None; }
        void clear() noexcept {
            code = CSVErrorCode::None;
            system.clear();
            context = csv::string_view();
            owned_context_.reset();
        }
        std::string message() const {
            const std::string text = context.empty() ? std::string() : std::string(context);
            return system ? text + ": " + system.message() : text;
        }

    private:
        // Keep success and borrowed static diagnostics allocation-free.
        std::unique_ptr<char[]> owned_context_;
    };

    namespace internals {
        inline bool fail(CSVError& error, CSVErrorCode code, csv::string_view message) {
            error = CSVError(code, message);
            return false;
        }

        // Use only for literals or other text with static lifetime. Runtime
        // pointers (notably exception::what()) must use the owning fail().
        inline bool fail_static(CSVError& error, CSVErrorCode code, csv::string_view message) noexcept {
            error = CSVError::from_static(code, message);
            return false;
        }
    }
}
