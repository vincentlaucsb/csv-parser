/** @file
  *  A standalone header file for writing delimiter-separated files
  */

#pragma once
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
#include "data_type.hpp"

namespace csv {
    namespace internals {
        static int DECIMAL_PLACES = 5;

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

            while (x >= 1) {
                x /= 10;
                digits++;
            }

            return digits;
        }

        /** to_string() for unsigned integers */
        template<typename T,
            csv::enable_if_t<std::is_unsigned<T>::value, int> = 0>
        inline std::string to_string(T value) {
            std::string digits_reverse = "";

            if (value == 0) return "0";

            while (value > 0) {
                digits_reverse += (char)('0' + (value % 10));
                value /= 10;
            }

            return std::string(digits_reverse.rbegin(), digits_reverse.rend());
        }

        /** to_string() for signed integers */
        template<
            typename T,
            csv::enable_if_t<std::is_integral<T>::value && std::is_signed<T>::value, int> = 0
        >
        inline std::string to_string(T value) {
            if (value >= 0)
                return to_string((size_t)value);

            return "-" + to_string((size_t)(value * -1));
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
     *  @tparam Flush        True: flush after every writing function,
     *                       false: you need to flush explicitly if needed.
     *                       In both cases the destructor will flush.
     *
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
    template<class OutputStream, char Delim, char Quote, bool Flush>
    class DelimWriter {
    public:
        /** Construct a DelimWriter over the specified output stream. */

        DelimWriter(OutputStream& _out, bool _quote_minimal = true)
            : out(&_out), quote_minimal(_quote_minimal) {}

        /** Construct a DelimWriter that owns an output file stream. */
        template<typename T = OutputStream,
            csv::enable_if_t<std::is_same<T, std::ofstream>::value, int> = 0>
        DelimWriter(const std::string& filename, bool _quote_minimal = true)
            : owned_out(new std::ofstream(filename, std::ios::out)),
            out(owned_out.get()),
            quote_minimal(_quote_minimal) {
            if (!owned_out->is_open())
                throw std::runtime_error("Failed to open file for writing: " + filename);
        }

        DelimWriter(const DelimWriter&) = delete;
        DelimWriter& operator=(const DelimWriter&) = delete;

        DelimWriter(DelimWriter&& other) noexcept
            : owned_out(std::move(other.owned_out)),
            out(other.out),
            quote_minimal(other.quote_minimal),
            batch_buffer_(std::move(other.batch_buffer_)) {
            if (owned_out) {
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
            batch_buffer_ = std::move(other.batch_buffer_);

            if (owned_out) {
                out = owned_out.get();
            }

            other.out = nullptr;
            other.quote_minimal = true;
            return *this;
        }

        /** Destructor will flush remaining data. */
        ~DelimWriter() {
            if (out) {
                flush_batch();
                out->flush();
            }
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

        /** Write a row by index using a field getter callback.
         *
         *  This keeps bulk writer integrations on the same escaping path without
         *  materializing an intermediate container of strings.
         */
        template<typename FieldGetter>
        DelimWriter& write_indexed_row(size_t field_count, FieldGetter&& get_field) {
            if (field_count != 0) {
                write_field(get_field(0));

                for (size_t i = 1; i < field_count; ++i) {
                    batch_buffer_.push_back(Delim);
                    write_field(get_field(i));
                }
            }

            end_record();
            finish_write_call();
            return *this;
        }

        /** Flushes the written data. */
        void flush() {
            flush_batch();
            out->flush();
        }

    private:
        /** Helper to write a range of values, handling first element undelimited,
         *  rest prefixed with delimiter. Inlines aggressively across both C++20 and
         *  C++11 operator<< entry points.
         */
        template<typename Range>
        inline void write_range_impl(const Range& record) {
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

        template<std::ranges::input_range Range>
            requires std::convertible_to<std::ranges::range_reference_t<Range>, csv::string_view>
        void append_range_fields(Range&& record) {
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
                return;
            }

            const std::string serialized(in);
            write_escaped_field(serialized);
        }

        void write_raw(csv::string_view in) {
            if (!in.empty()) {
                batch_buffer_.append(in.data(), in.size());
            }
        }

        size_t find_first_special_for_writer(csv::string_view in) const {
            size_t pos = internals::find_next_non_special(in, 0, simd_sentinels_);

            for (; pos < in.size(); ++pos) {
                char ch = in[pos];
                if (ch == Quote || ch == Delim || ch == '\r' || ch == '\n') {
                    return pos;
                }
            }

            return in.size();
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
            IF_CONSTEXPR(Flush) {
                flush_batch();
                out->flush();
                return;
            }

            flush_batch_if_needed();
        }

        void flush_batch_if_needed() {
            if (batch_buffer_.size() >= batch_flush_threshold_) {
                flush_batch();
            }
        }

        void flush_batch() {
            if (batch_buffer_.empty()) {
                return;
            }

            out->write(batch_buffer_.data(), static_cast<std::streamsize>(batch_buffer_.size()));
            batch_buffer_.clear();
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
        static constexpr size_t batch_flush_threshold_ = 64 * 1024;
        std::string batch_buffer_;
        internals::SentinelVecs simd_sentinels_{Delim, Quote};
    };

    /** An alias for csv::DelimWriter for writing standard CSV files
     *
     *  @sa csv::DelimWriter::operator<<()
     *
     *  @note Use `csv::make_csv_writer()` to in instatiate this class over
     *        an actual output stream.
     */
    template<class OutputStream, bool Flush = true>
    using CSVWriter = DelimWriter<OutputStream, ',', '"', Flush>;

    /** Class for writing tab-separated values files
    *
     *  @sa csv::DelimWriter::write_row()
     *  @sa csv::DelimWriter::operator<<()
     *
     *  @note Use `csv::make_tsv_writer()` to in instatiate this class over
     *        an actual output stream.
     */
    template<class OutputStream, bool Flush = true>
    using TSVWriter = DelimWriter<OutputStream, '\t', '"', Flush>;

    /** Return a csv::CSVWriter over the output stream */
    template<class OutputStream>
    inline CSVWriter<OutputStream> make_csv_writer(OutputStream& out, bool quote_minimal=true) {
        return CSVWriter<OutputStream>(out, quote_minimal);
    }

    /** Return a buffered csv::CSVWriter over the output stream (does not auto flush) */
    template<class OutputStream>
    inline CSVWriter<OutputStream, false> make_csv_writer_buffered(OutputStream& out, bool quote_minimal=true) {
        return CSVWriter<OutputStream, false>(out, quote_minimal);
    }

    /** Return a csv::TSVWriter over the output stream */
    template<class OutputStream>
    inline TSVWriter<OutputStream> make_tsv_writer(OutputStream& out, bool quote_minimal=true) {
        return TSVWriter<OutputStream>(out, quote_minimal);
    }

    /** Return a buffered csv::TSVWriter over the output stream (does not auto flush) */
    template<class OutputStream>
    inline TSVWriter<OutputStream, false> make_tsv_writer_buffered(OutputStream& out, bool quote_minimal=true) {
        return TSVWriter<OutputStream, false>(out, quote_minimal);
    }
    ///@}
}
