/** @file
  *  A standalone header file for writing delimiter-separated files
  */

#pragma once
#include "common.hpp"
#include <cmath>
#include <fstream>
#include <iostream>
#include <memory>
#ifdef CSV_HAS_CXX20
#include <ranges>
#endif
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <vector>

#include "basic_csv_parser_simd.hpp"
#include "common.hpp"
#include "csv_exceptions.hpp"
#include "data_frame/fwd.hpp"

namespace csv {
namespace internals {
    static int DECIMAL_PLACES = 5;

        /** Compute 10 to the power of an integral exponent. */
        template<typename T>
        CSV_CONST CONSTEXPR_14
        long double pow10(const T& n) noexcept {
            static_assert(std::is_integral<T>::value, "pow10 only supports integral exponents");

            long double multiplicand = n > 0 ? 10 : 0.1,
                ret = 1;
            T iterations = n > 0 ? n : -n;

            for (T i = 0; i < iterations; i++) {
                ret *= multiplicand;
            }

            return ret;
        }

        template<>
        CSV_CONST CONSTEXPR_14
        long double pow10(const unsigned& n) noexcept {
            long double multiplicand = n > 0 ? 10 : 0.1,
                ret = 1;

            for (unsigned i = 0; i < n; i++) {
                ret *= multiplicand;
            }

            return ret;
        }

        /**
         * Calculate the absolute value of a number
         */
        template<typename T = int>
        inline T csv_abs(T x) {
            return abs(x);
        }

        template<>
        inline int csv_abs(int x) {
            return abs(x);
        }

        template<>
        inline long int csv_abs(long int x) {
            return labs(x);
        }

        template<>
        inline long long int csv_abs(long long int x) {
            return llabs(x);
        }

        template<>
        inline float csv_abs(float x) {
            return fabsf(x);
        }

        template<>
        inline double csv_abs(double x) {
            return fabs(x);
        }

        template<>
        inline long double csv_abs(long double x) {
            return fabsl(x);
        }

        /** 
         * Calculate the number of digits in a number
         */
        template<
            typename T,
            csv::enable_if_t<std::is_arithmetic<T>::value, int> = 0
        >
        int num_digits(T x)
        {
            x = csv_abs(x);
            int digits = 0;
            for (; x >= 1; digits++)
                x /= 10;

            return (x == 0) ? 1 : digits;
        }

        /** to_string() for unsigned integers */
        template<typename T,
            csv::enable_if_t<std::is_unsigned<T>::value, int> = 0>
        inline std::string to_string(T value) {
            std::string digits_reverse = "";
            if (value == 0) return "0";

            for (; value > 0; value /= 10)
                digits_reverse += (char)('0' + (value % 10));

            return std::string(digits_reverse.rbegin(), digits_reverse.rend());
        }

        /** to_string() for signed integers */
        template<
            typename T,
            csv::enable_if_t<std::is_integral<T>::value && std::is_signed<T>::value, int> = 0
        >
        inline std::string to_string(T value) {
            return (value >= 0) ? to_string(static_cast<unsigned long long>(value))
                : "-" + to_string(0ULL - static_cast<unsigned long long>(value));
        }

    /** to_string() for floating point numbers */
    template<
        typename T,
        csv::enable_if_t<std::is_floating_point<T>::value, int> = 0
    >
    inline std::string to_string(T value) {
        std::string result = "";

            long double integral_part;
            long double fractional_part = csv_abs(std::modf((long double)value, &integral_part));

            const long double scale = pow10(DECIMAL_PLACES);
            long double rounded_fractional = std::round(fractional_part * scale);

            // Work with the absolute value of the integral part so digit extraction
            // and carry both work correctly for negative numbers.
            long double abs_integral = csv_abs(integral_part);

            // Carry rounding overflow from fractional digits into integral digits.
            if (rounded_fractional >= scale) {
                abs_integral += 1;
                rounded_fractional = 0;
            }

            // Integral part
            if (value < 0) result = "-";

            if (abs_integral == 0) {
                result += "0";
            }
            else {
                for (int n_digits = num_digits(abs_integral); n_digits > 0; n_digits --) {
                    int digit = (int)(std::fmod(abs_integral, pow10(n_digits)) / pow10(n_digits - 1));
                    result += (char)('0' + digit);
                }
            }

            // Decimal part
            result += ".";

            if (rounded_fractional > 0) {
                for (int n_digits = DECIMAL_PLACES; n_digits > 0; n_digits--) {
                    int digit = (int)(std::fmod(rounded_fractional, pow10(n_digits)) / pow10(n_digits - 1));
                    result += (char)('0' + digit);
                }
            }
            else {
                result += "0";
            }

        return result;
    }
}

/** Sets how many places after the decimal will be written for floating point numbers. */
inline static void set_decimal_places(int precision) {
    internals::DECIMAL_PLACES = precision;
}

namespace internals {
    template<typename T, typename = void>
    struct has_checked_csv_fields : std::false_type {};

    template<typename T>
    struct has_checked_csv_fields<T, csv::void_t<
        decltype(std::declval<const typename std::remove_reference<T>::type&>().size()),
        decltype(std::declval<const typename std::remove_reference<T>::type&>()
            .try_field(size_t{0}, std::declval<CSVError&>()).get_sv())>> : std::true_type {};

    template<typename T>
    struct is_data_frame : std::false_type {};

    template<typename KeyType>
    struct is_data_frame<DataFrame<KeyType>> : std::true_type {};

    /** SFINAE trait: detects if a type is iterable (has std::begin/end). */
    template<typename T, typename = void>
    struct is_iterable : std::false_type {};

        template<typename T>
        struct is_iterable<T, typename std::enable_if<true>::type> {
        private:
            template<typename U>
            static auto test(int) -> decltype(
                std::begin(std::declval<const U&>()),
                std::end(std::declval<const U&>()),
                std::true_type{}
            );
            template<typename>
            static std::false_type test(...);
        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

    /** SFINAE trait: detects if a type is a std::tuple. */
    template<typename T, typename = void>
    struct is_tuple : std::false_type {};

        template<typename T>
        struct is_tuple<T, typename std::enable_if<true>::type> {
        private:
            template<typename U>
            static auto test(int) -> decltype(std::tuple_size<U>::value, std::true_type{});
            template<typename>
            static std::false_type test(...);
        public:
            static constexpr bool value = decltype(test<T>(0))::value;
        };

}

/** @name CSV Writing */
///@{
    /** 
     *  Class for writing delimiter separated values files
     *
     *  To write formatted strings, one should
     *   -# Initialize a DelimWriter with respect to some output stream 
     *   -# Call write_row() on std::vector<std::string>s of unformatted text
     *
     *  @tparam OutputStream The output stream, e.g. `std::ofstream`, `std::stringstream`
     *  @tparam Delim        The delimiter character
     *  @tparam Quote        The quote character
     *  @par Hint
     *  Use the aliases csv::CSVWriter<OutputStream> to write CSV
     *  formatted strings and csv::TSVWriter<OutputStream>
     *  to write tab separated strings
     *
     *  @par Example w/ std::vector, std::deque, std::list
     *  @snippet test_write_csv.cpp CSV Writer Example
     *
     *  @par Example w/ std::tuple
     *  @snippet test_write_csv.cpp CSV Writer Tuple Example
     */
    template<class OutputStream, char Delim, char Quote>
    class DelimWriter {
    public:
        /** Construct a DelimWriter over the specified output stream. */

        DelimWriter(OutputStream& _out, bool _quote_minimal = true)
            : out(&_out), quote_minimal(_quote_minimal) {}

        /** Construct a DelimWriter that owns an output file stream. */
#if CSV_EXCEPTIONS_ENABLED
        template<typename T = OutputStream,
            csv::enable_if_t<std::is_same<T, std::ofstream>::value, int> = 0>
        DelimWriter(const std::string& filename, bool _quote_minimal = true)
            : DelimWriter(std::unique_ptr<OutputStream>(new OutputStream(filename, std::ios::out)),
                _quote_minimal) {
            if (!owned_out->is_open())
                internals::throw_failed_open_for_writing(filename);
        }
#endif

        /** Open an owned output file, returning null on failure.
         *  The returned writer owns its stream. Use try_close() to observe final I/O errors.
         */
        template<typename T = OutputStream,
            csv::enable_if_t<std::is_same<T, std::ofstream>::value, int> = 0>
        static std::unique_ptr<DelimWriter> try_open(
            const std::string& filename, CSVError& error, bool quote_minimal = true
        ) {
            error.clear();
            std::unique_ptr<OutputStream> stream(new OutputStream(filename, std::ios::out));
            if (!stream->is_open()) {
                internals::fail(error, CSVErrorCode::OpenFailed,
                    std::string(internals::ERROR_FAILED_OPEN_WRITE) + filename);
                return nullptr;
            }
            return std::unique_ptr<DelimWriter>(new DelimWriter(std::move(stream), quote_minimal));
        }

        DelimWriter(const DelimWriter&) = delete;
        DelimWriter& operator=(const DelimWriter&) = delete;

        DelimWriter(DelimWriter&& other) noexcept
            : owned_out(std::move(other.owned_out)),
            out(other.out),
            quote_minimal(other.quote_minimal),
            auto_flush_(other.auto_flush_),
            batch_buffer_(std::move(other.batch_buffer_)) {
            if (owned_out && out) {
                out = owned_out.get();
            }
            other.out = nullptr;
            other.quote_minimal = true;
        }

        DelimWriter& operator=(DelimWriter&& other) noexcept {
            if (this == &other) return *this;

            owned_out = std::move(other.owned_out);
            out = other.out;
            quote_minimal = other.quote_minimal;
            auto_flush_ = other.auto_flush_;
            batch_buffer_ = std::move(other.batch_buffer_);

            if (owned_out && out) {
                out = owned_out.get();
            }

            other.out = nullptr;
            other.quote_minimal = true;
            return *this;
        }

        /** Configure whether each write operation flushes the underlying stream. */
        DelimWriter& set_auto_flush(bool value) & noexcept {
            this->auto_flush_ = value;
            return *this;
        }

        /** Configure whether each write operation flushes the underlying stream. */
        DelimWriter&& set_auto_flush(bool value) && noexcept {
            this->auto_flush_ = value;
            return std::move(*this);
        }

        /** Return whether each write operation flushes the underlying stream. */
        bool get_auto_flush() const noexcept {
            return this->auto_flush_;
        }

        /** Best-effort flush. Use try_flush() or try_close() to observe errors. */
        ~DelimWriter() {
            // A failed write can have emitted a prefix. Never retry its buffered bytes
            // during destruction; the caller has already received the I/O failure.
            if (!out || !ready_for_destruction(*out, 0)) return;
            CSV_TRY {
                flush_batch();
                out->flush();
            }
            CSV_CATCH((...), {
                // Destruction must not replace an active exception or terminate a caller
                // that already handled a checked I/O failure.
            })
        }

        /** Write a C-style array of strings as one delimited row. */
        template<typename T, size_t N>
        DelimWriter& operator<<(const T (&record)[N]) {
            write_range_impl(record);
            return *this;
        }

        /** Write a std::array of strings as one delimited row. */
        template<typename T, size_t N>
        DelimWriter& operator<<(const std::array<T, N>& record) {
            write_range_impl(record);
            return *this;
        }

        /** Write a complete DataFrame: visible column names followed by visible rows.
         *  Sparse edits and row selections are included. A frame with no rows still
         *  writes its header. The writer's delimiter, quoting, and buffering settings
         *  apply, and auto-flush flushes the stream once at the end of the document.
         */
#if CSV_EXCEPTIONS_ENABLED
        template<typename KeyType>
        DelimWriter& operator<<(const DataFrame<KeyType>& frame) {
            append_range_fields(frame.columns());
            end_record();
            flush_batch_if_needed();

#ifdef CSV_HAS_CXX20
            return write_rows(frame);
#else
            // Older standards use the existing visible-row conversion path.
            for (const auto& row : frame) {
                append_range_fields(std::vector<std::string>(row));
                end_record();
                flush_batch_if_needed();
            }
            finish_write_call();
            return *this;
#endif
        }

#endif

        /** Write a row from any single argument accepted by operator<<
         *  (std::vector, std::array, std::tuple, C-array, C++20 range, etc.).
         *
         *  @code
         *  writer.write_row(my_vector);
         *  writer.write_row(my_tuple);
         *  @endcode
         *
         *  SFINAE ensures this overload is only viable when the argument type
         *  is accepted by an existing operator<< overload.
         */
        template<typename T>
        auto write_row(T&& record) -> decltype(*this << std::forward<T>(record)) {
            return *this << std::forward<T>(record);
        }

        /** Write a row from a variadic list of mixed-type values.
         *
         *  Requires at least two arguments; for a single container or tuple,
         *  use the single-argument overload above.
         *
         *  @code
         *  writer.write_row("Alice", 30, 1.75, "Paris");
         *  @endcode
         */
        template<typename T, typename U, typename... Rest>
        DelimWriter& write_row(T&& first, U&& second, Rest&&... rest) {
            this->write_tuple<0>(std::forward_as_tuple(
                std::forward<T>(first), std::forward<U>(second), std::forward<Rest>(rest)...));
            return *this;
        }

#ifdef CSV_HAS_CXX20
        /** Write many rows using a shared batch buffer.
         *
         *  Accepts any input_range whose elements are either:
         *   - ranges of string-like fields, or
         *   - row-like objects exposing to_sv_range().
         *
         *  Buffered writers flush the batch when it grows large or when this call ends.
         *  Auto-flushing writers additionally flush the underlying stream once at the
         *  end of the bulk call.
         *
         *  Passing a DataFrame writes data rows only. Use operator<< to include
         *  its column names as a header.
         *
         *  @note Requires C++20 or later.
         */
        template<std::ranges::input_range Rows>
            requires internals::csv_write_rows_input_range<Rows>
        DelimWriter& write_rows(Rows&& rows) {
            for (auto&& row : rows) {
                append_row_like(row);
                flush_batch_if_needed();
            }

            finish_write_call();
            return *this;
        }

        /** Write a range of string-like fields as one delimited row.
         *
         *  Accepts any input_range whose elements are convertible to csv::string_view.
         *  This includes std::vector<std::string>, std::vector<csv::string_view>,
         *  std::array, C++20 views, etc.
         */
        template<std::ranges::input_range Range>
        DelimWriter& operator<<(Range&& container)
            requires std::ranges::input_range<Range>
                && std::convertible_to<std::ranges::range_reference_t<Range>, csv::string_view> {
            write_range_impl(container);
            return *this;
        }

        /** Write a row-like object that exposes to_sv_range(). */
        template<typename RowLike>
        DelimWriter& operator<<(const RowLike& row)
            requires internals::has_to_sv_range<RowLike>
                && (!internals::csv_string_field_range<RowLike>) {
            append_row_like(row);
            finish_write_call();
            return *this;
        }
#else
        /** Write a range of string-like fields as one delimited row.
         *
         *  Accepts any input_range whose elements are convertible to csv::string_view.
         *  This includes std::vector<std::string>, std::vector<csv::string_view>,
         *  std::array, C++20 views, etc.
         *
         *  @note Implementation detail: Uses SFINAE for runtime compatibility.
         */
        template<typename Range>
        typename std::enable_if<
            internals::is_iterable<Range>::value 
            && !internals::is_tuple<Range>::value
            && !std::is_same<Range, std::string>::value
            && !std::is_same<Range, csv::string_view>::value,
            DelimWriter&
        >::type operator<<(const Range& record) {
            write_range_impl(record);
            return *this;
        }
#endif

        /** @copydoc operator<< */
        template<typename... T>
        DelimWriter& operator<<(const std::tuple<T...>& record) {
            this->write_tuple<0, T...>(record);
            return *this;
        }

        /** Flushes the written data. */
        void flush() {
            flush_batch();
            out->flush();
        }

        /** Write the same inputs accepted by write_row(), reporting stream failures.
         *  With buffering enabled, success means accepted into the buffer; try_flush()
         *  or try_close() observes completion. Output already written is not rolled back.
         *  Checked operations require a stream exposing fail(). In exception-disabled
         *  builds, standard streams must have their exception mask disabled.
         */
        template<typename T,
            csv::enable_if_t<internals::has_checked_csv_fields<T>::value
                || internals::is_data_frame<typename std::decay<T>::type>::value, int> = 0>
        bool try_write_row(CSVError& error, T&& record) {
            return this->checked_write(error, std::forward<T>(record));
        }

        template<typename... Args>
        auto try_write_row(CSVError& error, Args&&... args)
            -> decltype(this->write_row(std::forward<Args>(args)...), bool()) {
            return this->checked_write(error, std::forward<Args>(args)...);
        }

#ifdef CSV_HAS_CXX20
        /** Write a batch using the same accepted row ranges and buffering policy as write_rows(). */
        template<std::ranges::input_range Rows>
            requires (internals::csv_write_rows_input_range<Rows>
                || internals::has_checked_csv_fields<std::ranges::range_reference_t<Rows>>::value)
        bool try_write_rows(Rows&& rows, CSVError& error) {
            return this->checked_operation(error, [this, &rows, &error]() {
                for (auto&& row : rows) {
                    if (!this->append_checked_row(row, error, 0)) return false;
                    this->flush_batch_if_needed();
                    if (!this->check_write_status(error)) return false;
                }
                this->finish_write_call();
                return true;
            });
        }
#endif

        /** Flush buffered data and the stream, reporting I/O errors. */
        bool try_flush(CSVError& error) {
            return this->checked_operation(error, [this]() {
                this->flush();
                return true;
            });
        }

        /** Flush and close an owned output file, including after a flush failure.
         *  Borrowed streams remain the caller's responsibility to close.
         *  Further checked operations on a closed writer report InvalidArgument.
         */
        template<typename T = OutputStream,
            csv::enable_if_t<std::is_same<T, std::ofstream>::value, int> = 0>
        bool try_close(CSVError& error) {
            error.clear();
            if (!owned_out || !out) {
                return internals::fail_static(error, CSVErrorCode::InvalidArgument,
                    "try_close() requires an open, owned output file.");
            }

            const bool flushed = this->try_flush(error);
            // Detach before closing so neither close errors nor a failed flush can
            // trigger a second write from the writer's destructor.
            out = nullptr;
            CSV_TRY {
                owned_out->close();
            }
            CSV_CATCH((const std::ios_base::failure& failure), {
                if (flushed) internals::fail(error, CSVErrorCode::WriteFailed, failure.what());
                return false;
            })
            if (!flushed) return false;
            if (owned_out->fail()) {
                return internals::fail_static(error, CSVErrorCode::WriteFailed, "Failed to close CSV output.");
            }
            return true;
        }

    private:
        DelimWriter(std::unique_ptr<OutputStream> stream, bool quote_minimal)
            : owned_out(std::move(stream)), out(owned_out.get()), quote_minimal(quote_minimal) {}

        // These probes preserve support for lightweight custom output streams in
        // the existing unchecked API. Only checked calls require fail().
        template<typename Stream>
        static auto exceptions_disabled(const Stream& stream, int)
            -> decltype(stream.exceptions(), bool()) {
            return stream.exceptions() == std::ios_base::goodbit;
        }

        template<typename Stream>
        static bool exceptions_disabled(const Stream&, long) { return true; }

        template<typename Stream>
        static auto ready_for_destruction(const Stream& stream, int)
            -> decltype(stream.fail(), bool()) {
#if CSV_EXCEPTIONS_ENABLED
            return !stream.fail();
#else
            return !stream.fail() && exceptions_disabled(stream, 0);
#endif
        }

        template<typename Stream>
        static bool ready_for_destruction(const Stream&, long) { return true; }

        template<typename Fn>
        bool checked_operation(CSVError& error, Fn&& operation) {
            error.clear();
            if (!out) {
                return internals::fail_static(error, CSVErrorCode::InvalidArgument, "CSV output is closed.");
            }
#if !CSV_EXCEPTIONS_ENABLED
            if (!exceptions_disabled(*out, 0)) {
                return internals::fail_static(error, CSVErrorCode::InvalidArgument,
                    "Checked CSV output requires a stream with exceptions disabled.");
            }
#endif
            if (out->fail()) {
                return internals::fail_static(error, CSVErrorCode::WriteFailed, "CSV output stream has failed.");
            }
            CSV_TRY {
                if (!std::forward<Fn>(operation)()) return false;
            }
            CSV_CATCH((const std::ios_base::failure& failure), {
                return internals::fail(error, CSVErrorCode::WriteFailed, failure.what());
            })
            return this->check_write_status(error);
        }

        bool check_write_status(CSVError& error) const {
            return !out->fail() || internals::fail_static(error, CSVErrorCode::WriteFailed,
                "CSV output stream has failed.");
        }

        template<typename... Args>
        bool checked_write(CSVError& error, Args&&... args) {
            return this->checked_operation(error, [&]() {
                return this->checked_write_call(error, std::forward<Args>(args)...);
            });
        }

        template<typename Row>
        static auto checked_record_valid(const Row& row, int)
            -> decltype(row.is_valid(), bool()) {
            return row.is_valid();
        }

        template<typename Row>
        static bool checked_record_valid(const Row&, long) { return true; }

        template<typename T>
        bool checked_write_call(CSVError& error, T&& record) {
            return this->checked_write_record(std::forward<T>(record), error, 0);
        }

        template<typename T, typename U, typename... Rest>
        bool checked_write_call(CSVError&, T&& first, U&& second, Rest&&... rest) {
            this->write_row(std::forward<T>(first), std::forward<U>(second), std::forward<Rest>(rest)...);
            return true;
        }

        template<typename KeyType>
        bool checked_write_record(DataFrame<KeyType>& frame, CSVError& error, int) {
            return this->checked_write_record(static_cast<const DataFrame<KeyType>&>(frame), error, 0);
        }

        template<typename KeyType>
        bool checked_write_record(DataFrame<KeyType>&& frame, CSVError& error, int) {
            return this->checked_write_record(static_cast<const DataFrame<KeyType>&>(frame), error, 0);
        }

        template<typename KeyType>
        bool checked_write_record(const DataFrame<KeyType>& frame, CSVError& error, int) {
            append_range_fields(frame.columns());
            end_record();
            flush_batch_if_needed();
            if (!this->check_write_status(error)) return false;
            for (const auto& row : frame) {
                if (!this->append_checked_row(row, error, 0)) return false;
                flush_batch_if_needed();
                if (!this->check_write_status(error)) return false;
            }
            finish_write_call();
            return true;
        }

        template<typename Row>
        auto checked_write_record(Row&& row, CSVError& error, int)
            -> decltype(row.try_field(size_t{0}, error).get_sv(), bool()) {
            if (!this->append_checked_row(row, error, 0)) return false;
            finish_write_call();
            return true;
        }

        template<typename Row>
        bool checked_write_record(Row&& row, CSVError&, long) {
            this->write_row(std::forward<Row>(row));
            return true;
        }

        // Logical rows need checked physical-width access: KEEP permits native
        // rows shorter than the schema. Generic string ranges use their iterator
        // contract instead, so this indexed validation cannot share that loop.
        template<typename Row>
        auto append_checked_row(Row&& row, CSVError& error, int)
            -> decltype(row.try_field(size_t{0}, error).get_sv(), bool()) {
            if (!checked_record_valid(row, 0)) {
                return internals::fail_static(error, CSVErrorCode::InvalidProxy, "CSV row or column proxy is invalid.");
            }
            const size_t row_start = batch_buffer_.size();
            for (size_t i = 0; i < row.size(); ++i) {
                const auto field = row.try_field(i, error);
                if (error) {
                    // No bytes from this row have been sent yet. Discard its
                    // incomplete buffered record while retaining earlier rows.
                    batch_buffer_.resize(row_start);
                    return false;
                }
                if (i != 0) batch_buffer_.push_back(Delim);
                write_escaped_field(field.get_sv());
            }
            end_record();
            return true;
        }

#ifdef CSV_HAS_CXX20
        template<typename Row>
        bool append_checked_row(Row&& row, CSVError&, long) {
            append_row_like(std::forward<Row>(row));
            return true;
        }
#endif

        /** Append delimited fields from a range without terminating the record.
         *
         *  Shared by single-row writes and bulk row appends so escaping and
         *  delimiter handling stay on one code path.
         */
        template<typename Range>
        inline void append_range_fields(Range&& record) {
            auto it = std::begin(record);
            auto end = std::end(record);

            if (it != end) {
                write_field(*it);
                ++it;
            }

            for (; it != end; ++it) {
                batch_buffer_.push_back(Delim);
                write_field(*it);
            }
        }

        /** SIMD fast-forward for quote escape logic */
        size_t find_first_special_for_writer(csv::string_view in) const {
            size_t pos = internals::find_next_non_special(in, 0, simd_sentinels_);

            for (; pos < in.size(); ++pos) {
                char ch = in[pos];
                if (ch == Quote || ch == Delim || ch == '\r' || ch == '\n')
                    return pos;
            }

            return in.size();
        }

        /** Helper to write a complete range-backed row and apply the normal
         *  end-of-record and flush policy for operator<< entry points.
         */
        template<typename Range>
        inline void write_range_impl(const Range& record) {
            append_range_fields(record);

            end_record();
            finish_write_call();
        }

#ifdef CSV_HAS_CXX20
        template<typename Row>
        void append_row_like(Row&& row) {
            IF_CONSTEXPR(internals::csv_string_field_range<Row>) {
                append_range_fields(std::forward<Row>(row));
            }
            else {
                append_range_fields(row.to_sv_range());
            }

            end_record();
        }
#endif

        template<
            typename T,
            csv::enable_if_t<
                !std::is_convertible<T, std::string>::value
                && !std::is_convertible<T, csv::string_view>::value
            , int> = 0
        >
        void write_field(T in) {
            const std::string serialized = internals::to_string(in);
            write_raw(serialized);
        }

        template<
            typename T,
            csv::enable_if_t<
                std::is_convertible<T, std::string>::value
                || std::is_convertible<T, csv::string_view>::value
            , int> = 0
        >
        void write_field(T in) {
            IF_CONSTEXPR(std::is_convertible<T, csv::string_view>::value) {
                write_escaped_field(in);
            }
            else {
                const std::string serialized(in);
                write_escaped_field(serialized);
            }
        }

        void write_raw(csv::string_view in) {
            if (!in.empty())
                batch_buffer_.append(in.data(), in.size());
        }

        void write_escaped_field(csv::string_view in) {
            const size_t first_special = find_first_special_for_writer(in);

            if (first_special == in.size()) {
                if (!quote_minimal) {
                    batch_buffer_.push_back(Quote);
                    write_raw(in);
                    batch_buffer_.push_back(Quote);
                } else {
                    write_raw(in);
                }
                return;
            }

            write_quoted_field(in, first_special);
        }

        void write_quoted_field(csv::string_view in, size_t first_special) {
            batch_buffer_.push_back(Quote);

            size_t chunk_start = 0;
            size_t pos = first_special;
            while (pos < in.size()) {
                if (in[pos] == Quote) {
                    write_raw(in.substr(chunk_start, pos - chunk_start));
                    batch_buffer_.push_back(Quote);
                    batch_buffer_.push_back(Quote);
                    chunk_start = pos + 1;
                }

                ++pos;
            }

            write_raw(in.substr(chunk_start));
            batch_buffer_.push_back(Quote);
        }

        /** Recursive template for writing std::tuples */
        template<size_t Index = 0, typename... T>
        typename std::enable_if<Index < sizeof...(T), void>::type write_tuple(const std::tuple<T...>& record) {
            write_field(std::get<Index>(record));

            CSV_MSVC_PUSH_DISABLE(4127)
            IF_CONSTEXPR (Index + 1 < sizeof...(T)) batch_buffer_.push_back(Delim);
            CSV_MSVC_POP

            this->write_tuple<Index + 1>(record);
        }

        /** Base case for writing std::tuples */
        template<size_t Index = 0, typename... T>
        typename std::enable_if<Index == sizeof...(T), void>::type write_tuple(const std::tuple<T...>& record) {
            (void)record;
            end_record();
            finish_write_call();
        }

        /** Finalize a single CSV row inside the shared batch buffer. */
        void end_record() {
            batch_buffer_.push_back('\n');
        }

        void finish_write_call() {
            if (this->auto_flush_) {
                flush_batch();
                out->flush();
                return;
            }

            flush_batch_if_needed();
        }

        void flush_batch() {
            if (batch_buffer_.empty()) return;

            out->write(batch_buffer_.data(), static_cast<std::streamsize>(batch_buffer_.size()));
            batch_buffer_.clear();
        }

        void flush_batch_if_needed() {
            if (batch_buffer_.size() >= batch_flush_threshold_)
                flush_batch();
        }

        /**
         * An owned output stream, if the writer owns it.
         * May be null if the writer does not own its output stream, i.e.
         * if it was initialized with an output stream reference instead of a filename.
         */
        std::unique_ptr<OutputStream> owned_out;

        /** Pointer to the output stream (which may or may not be owned by this writer). */
        OutputStream* out;

        bool quote_minimal;
        bool auto_flush_ = true;
        static constexpr size_t batch_flush_threshold_ = 64 * 1024;
        std::string batch_buffer_;
        internals::SentinelVecs simd_sentinels_{Delim, Quote};
    };

    /** An alias for csv::DelimWriter for writing standard CSV files
     *
     *  @sa csv::DelimWriter::operator<<()
     *
     *  @note Use `csv::make_csv_writer()` to instantiate this class over
     *        an actual output stream.
     */
    template<class OutputStream>
    using CSVWriter = DelimWriter<OutputStream, ',', '"'>;

    /** Class for writing tab-separated values files
    *
     *  @sa csv::DelimWriter::write_row()
     *  @sa csv::DelimWriter::operator<<()
     *
     *  @note Use `csv::make_tsv_writer()` to instantiate this class over
     *        an actual output stream.
     */
    template<class OutputStream>
    using TSVWriter = DelimWriter<OutputStream, '\t', '"'>;

    /** Return a csv::CSVWriter over the output stream */
    template<class OutputStream>
    inline CSVWriter<OutputStream> make_csv_writer(OutputStream& out, bool quote_minimal=true) {
        return CSVWriter<OutputStream>(out, quote_minimal);
    }

    /** Return a csv::TSVWriter over the output stream */
    template<class OutputStream>
    inline TSVWriter<OutputStream> make_tsv_writer(OutputStream& out, bool quote_minimal=true) {
        return TSVWriter<OutputStream>(out, quote_minimal);
    }
    ///@}
}
