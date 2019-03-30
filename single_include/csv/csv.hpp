#include <algorithm>
#include <cctype>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <deque>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <limits>
#include <math.h>
#include <memory>
#include <mutex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <vector>
namespace csv { 
    /** @file
     *  A standalone header file for writing delimiter-separated files
     */

    /** @name CSV Writing */
    ///@{
    #ifndef DOXYGEN_SHOULD_SKIP_THIS
    template<char Delim = ',', char Quote = '"'>
    inline std::string csv_escape(const std::string& in, const bool quote_minimal = true) {
        /** Format a string to be RFC 4180-compliant
         *  @param[in]  in              String to be CSV-formatted
         *  @param[out] quote_minimal   Only quote fields if necessary.
         *                              If False, everything is quoted.
         */

        std::string new_string;
        bool quote_escape = false;     // Do we need a quote escape
        new_string += Quote;           // Start initial quote escape sequence

        for (size_t i = 0; i < in.size(); i++) {
            switch (in[i]) {
            case Quote:
                new_string += std::string(2, Quote);
                quote_escape = true;
                break;
            case Delim:
                quote_escape = true;
                // Do not break;
            default:
                new_string += in[i];
            }
        }

        if (quote_escape || !quote_minimal) {
            new_string += Quote; // Finish off quote escape
            return new_string;
        }
        else {
            return in;
        }
    }
    #endif

    /** 
     *  @brief Class for writing delimiter separated values files
     *
     *  To write formatted strings, one should
     *   -# Initialize a DelimWriter with respect to some output stream 
     *      (e.g. std::ifstream or std::stringstream)
     *   -# Call write_row() on std::vector<std::string>s of unformatted text
     *
     *  **Hint**: Use the aliases CSVWriter<OutputStream> to write CSV
     *  formatted strings and TSVWriter<OutputStream>
     *  to write tab separated strings
     */
    template<class OutputStream, char Delim, char Quote>
    class DelimWriter {
    public:
        DelimWriter(OutputStream& _out) : out(_out) {};
        DelimWriter(const std::string& filename) : DelimWriter(std::ifstream(filename)) {};

        void write_row(const std::vector<std::string>& record, bool quote_minimal = true) {
            /** Format a sequence of strings and write to CSV according to RFC 4180
            *
            *  **Note**: This does not check to make sure row lengths are consistent
            *  @param[in]  record          Vector of strings to be formatted
            *  @param      quote_minimal   Only quote fields if necessary
            */

            for (size_t i = 0, ilen = record.size(); i < ilen; i++) {
                out << csv_escape<Delim, Quote>(record[i]);
                if (i + 1 != ilen) out << Delim;
            }

            out << std::endl;
        }

        DelimWriter& operator<<(const std::vector<std::string>& record) {
            /** Calls write_row() on record */
            this->write_row(record);
            return *this;
        }

    private:
        OutputStream & out;
    };

    /* Uncomment when C++17 support is better
    template<class OutputStream>
    DelimWriter(OutputStream&) -> DelimWriter<OutputStream>;
    */

    /** @typedef CSVWriter
     *  @brief   Class for writing CSV files
     */
    template<class OutputStream>
    using CSVWriter = DelimWriter<OutputStream, ',', '"'>;

    /** @typedef TSVWriter
     *  @brief    Class for writing tab-separated values files
     */
    template<class OutputStream>
    using TSVWriter = DelimWriter<OutputStream, '\t', '"'>;

    //
    // Temporary: Until more C++17 compilers support template deduction guides
    //
    template<class OutputStream>
    inline CSVWriter<OutputStream> make_csv_writer(OutputStream& out) {
        /** Return a CSVWriter over the output stream */
        return CSVWriter<OutputStream>(out);
    }

    template<class OutputStream>
    inline TSVWriter<OutputStream> make_tsv_writer(OutputStream& out) {
        /** Return a TSVWriter over the output stream */
        return TSVWriter<OutputStream>(out);
    }

    ///@}

    /** Enumerates the different CSV field types that are
    *  recognized by this library
    *
    *  - 0. CSV_NULL (empty string)
    *  - 1. CSV_STRING
    *  - 2. CSV_INT
    *  - 3. CSV_LONG_INT
    *  - 4. CSV_LONG_LONG_INT
    *  - 5. CSV_DOUBLE
    *
    *  **Note**: Overflowing integers will be stored and classified as doubles.
    *  Furthermore, the same number may either be a CSV_LONG_INT or CSV_INT depending on
    *  compiler and platform.
    */
    enum DataType {
        CSV_NULL,
        CSV_STRING,
        CSV_INT,
        CSV_LONG_INT,
        CSV_LONG_LONG_INT,
        CSV_DOUBLE
    };

    namespace internals {
        template<typename T>
        DataType type_num();

        template<> inline DataType type_num<int>() { return CSV_INT; }
        template<> inline DataType type_num<long int>() { return CSV_LONG_INT; }
        template<> inline DataType type_num<long long int>() { return CSV_LONG_LONG_INT; }
        template<> inline DataType type_num<double>() { return CSV_DOUBLE; }
        template<> inline DataType type_num<long double>() { return CSV_DOUBLE; }
        template<> inline DataType type_num<std::nullptr_t>() { return CSV_NULL; }
        template<> inline DataType type_num<std::string>() { return CSV_STRING; }

        std::string type_name(const DataType&);
        DataType data_type(std::string_view in, long double* const out = nullptr);
    }

    #if defined(_WIN32)
        #include <Windows.h>
        #undef max
        #undef min
        inline int getpagesize() {
            _SYSTEM_INFO sys_info = {};
            GetSystemInfo(&sys_info);
            return sys_info.dwPageSize;
        }

        const int PAGE_SIZE = getpagesize();
    #elif defined(__linux__) 
        #include <unistd.h>
        const int PAGE_SIZE = getpagesize();
    #else
        const int PAGE_SIZE = 4096;
    #endif

    namespace internals {
        /** @struct ColNames
         *  @brief A data structure for handling column name information.
         *
         *  These are created by CSVReader and passed (via smart pointer)
         *  to CSVRow objects it creates, thus
         *  allowing for indexing by column name.
         */
        struct ColNames {
            ColNames(const std::vector<std::string>&);
            std::vector<std::string> col_names;
            std::unordered_map<std::string, size_t> col_pos;

            std::vector<std::string> get_col_names() const;
            size_t size() const;
        };
    }

    /**
    * @class CSVField
    * @brief Data type representing individual CSV values. 
    *        CSVFields can be obtained by using CSVRow::operator[]
    */
    class CSVField {
    public:
        CSVField(std::string_view _sv) : sv(_sv) { };

        /** Returns the value casted to the requested type, performing type checking before.
        *  An std::runtime_error will be thrown if a type mismatch occurs, with the exception
        *  of T = std::string, in which the original string representation is always returned.
        *  Converting long ints to ints will be checked for overflow.
        *
        *  **Valid options for T**:
        *   - std::string or std::string_view
        *   - int
        *   - long
        *   - long long
        *   - double
        *   - long double
        */
        template<typename T=std::string_view> T get() {
            auto dest_type = internals::type_num<T>();
            if (dest_type >= CSV_INT && is_num()) {
                if (internals::type_num<T>() < this->type())
                    throw std::runtime_error("Overflow error.");

                return static_cast<T>(this->value);
            }

            throw std::runtime_error("Attempted to convert a value of type " + 
                internals::type_name(type()) + " to " + internals::type_name(dest_type) + ".");
        }

        bool operator==(std::string_view other) const;
        bool operator==(const long double& other);

        DataType type();
        bool is_null() { return type() == CSV_NULL; }
        bool is_str() { return type() == CSV_STRING; }
        bool is_num() { return type() >= CSV_INT; }
        bool is_int() {
            return (type() >= CSV_INT) && (type() <= CSV_LONG_LONG_INT);
        }
        bool is_float() { return type() == CSV_DOUBLE; };

    private:
        long double value = 0;
        std::string_view sv;
        int _type = -1;
        void get_value();
    };

    /**
     * @class CSVRow 
     * @brief Data structure for representing CSV rows
     *
     * Internally, a CSVRow consists of:
     *  - A pointer to the original column names
     *  - A string containing the entire CSV row (row_str)
     *  - An array of positions in that string where individual fields begin (splits)
     *
     * CSVRow::operator[] uses splits to compute a string_view over row_str.
     *
     */
    class CSVRow {
    public:
        CSVRow() = default;
        CSVRow(
            std::shared_ptr<std::string> _str,
            std::string_view _row_str,
            std::vector<size_t>&& _splits,
            std::shared_ptr<internals::ColNames> _cnames = nullptr) :
            str(_str),
            row_str(_row_str),
            splits(std::move(_splits)),
            col_names(_cnames)
        {};

        CSVRow(
            std::string _row_str,
            std::vector<size_t>&& _splits,
            std::shared_ptr<internals::ColNames> _cnames = nullptr
            ) :
            str(std::make_shared<std::string>(_row_str)),
            splits(std::move(_splits)),
            col_names(_cnames)
        {
            row_str = std::string_view(this->str->c_str());
        };

        bool empty() const { return this->row_str.empty(); }
        size_t size() const;

        /** @name Value Retrieval */
        ///@{
        CSVField operator[](size_t n) const;
        CSVField operator[](const std::string&) const;
        std::string_view get_string_view(size_t n) const;
        operator std::vector<std::string>() const;
        ///@}

        /** @brief A random access iterator over the contents of a CSV row.
         *         Each iterator points to a CSVField.
         */
        class iterator {
        public:
            using value_type = CSVField;
            using difference_type = int;
            using pointer = CSVField * ;
            using reference = CSVField & ;
            using iterator_category = std::random_access_iterator_tag;

            iterator(const CSVRow*, int i);

            reference operator*() const;
            pointer operator->() const;

            iterator operator++(int);
            iterator& operator++();
            iterator operator--(int);
            iterator& operator--();
            iterator operator+(difference_type n) const;
            iterator operator-(difference_type n) const;

            bool operator==(const iterator&) const;
            bool operator!=(const iterator& other) const { return !operator==(other); }

        private:
            const CSVRow * daddy = nullptr;            // Pointer to parent
            std::shared_ptr<CSVField> field = nullptr; // Current field pointed at
            int i = 0;                                 // Index of current field
        };

        /** @brief A reverse iterator over the contents of a CSVRow. */
        using reverse_iterator = std::reverse_iterator<iterator>;

        /** @name Iterators
         *  @brief Each iterator points to a CSVField object.
         */
        ///@{
        iterator begin() const;
        iterator end() const;
        reverse_iterator rbegin() const;
        reverse_iterator rend() const;
        ///@}

    private:
        std::shared_ptr<internals::ColNames> col_names = nullptr;
        std::shared_ptr<std::string> str = nullptr;
        std::string_view row_str;
        std::vector<size_t> splits;
    };

    // get() specializations
    template<>
    inline std::string CSVField::get<std::string>() {
        return std::string(this->sv);
    }

    template<>
    inline std::string_view CSVField::get<std::string_view>() {
        return this->sv;
    }

    template<>
    inline long double CSVField::get<long double>() {
        if (!is_num())
            throw std::runtime_error("Not a number.");

        return this->value;
    }

    /** @brief Integer indicating a requested column wasn't found. */
    const int CSV_NOT_FOUND = -1;

    /** @brief Used for counting number of rows */
    using RowCount = long long int;
   
    class CSVRow;
    using CSVCollection = std::deque<CSVRow>;

    /** 
     *  @brief Stores information about how to parse a CSV file
     *
     *   - Can be used to initialize a csv::CSVReader() object
     *   - The preferred way to pass CSV format information between functions
     *
     *  @see csv::DEFAULT_CSV, csv::GUESS_CSV
     *
     */
    struct CSVFormat {
        char delim;
        char quote_char;

        /**< @brief Row number with columns (ignored if col_names is non-empty) */
        int header;

        /**< @brief Should be left empty unless file doesn't include header */
        std::vector<std::string> col_names;

        /**< @brief RFC 4180 non-compliance -> throw an error */
        bool strict;

        /**< @brief Detect and strip out Unicode byte order marks */
        bool unicode_detect;
    };

    /** Returned by get_file_info() */
    struct CSVFileInfo {
        std::string filename;               /**< Filename */
        std::vector<std::string> col_names; /**< CSV column names */
        char delim;                         /**< Delimiting character */
        RowCount n_rows;                    /**< Number of rows in a file */
        int n_cols;                         /**< Number of columns in a CSV */
    };

    /** @namespace csv::internals
     *  @brief Stuff that is generally not of interest to end-users
     */
    namespace internals {
        bool is_equal(double a, double b, double epsilon = 0.001);
        std::string type_name(const DataType& dtype);
        std::string format_row(const std::vector<std::string>& row, const std::string& delim = ", ");

        /** Class for reducing number of new string malloc() calls */
        struct GiantStringBuffer {
            std::string_view get_row();
            size_t size() const;
            std::string* get();
            std::string* operator->();
            void operator+=(const char);

            std::shared_ptr<std::string> buffer = nullptr;
            size_t current_end = 0;
            void reset();
        };
    }

    /** @name Global Constants */
    ///@{
    /** @brief For functions that lazy load a large CSV, this determines how
     *         many bytes are read at a time
     */
    const size_t ITERATION_CHUNK_SIZE = 10000000; // 10MB

    /** @brief A dummy variable used to indicate delimiter should be guessed */
    const CSVFormat GUESS_CSV = { '\0', '"', 0, {}, false, true };

    /** @brief RFC 4180 CSV format */
    const CSVFormat DEFAULT_CSV = { ',', '"', 0, {}, false, true };

    /** @brief RFC 4180 CSV format with strict parsing */
    const CSVFormat DEFAULT_CSV_STRICT = { ',', '"', 0, {}, true, true };
    ///@}

    /** @class CSVReader
     *  @brief Main class for parsing CSVs from files and in-memory sources
     *
     *  All rows are compared to the column names for length consistency
     *  - By default, rows that are too short or too long are dropped
     *  - Custom behavior can be defined by overriding bad_row_handler in a subclass
     */
    class CSVReader {
        public:
            /**
             * @brief An input iterator capable of handling large files.
             * Created by CSVReader::begin() and CSVReader::end().
             *
             * **Iterating over a file:**
             * \snippet tests/test_csv_iterator.cpp CSVReader Iterator 1
             *
             * **Using with <algorithm> library:**
             * \snippet tests/test_csv_iterator.cpp CSVReader Iterator 2
             */
            class iterator {
            public:
                using value_type = CSVRow;
                using difference_type = std::ptrdiff_t;
                using pointer = CSVRow * ;
                using reference = CSVRow & ;
                using iterator_category = std::input_iterator_tag;

                iterator() = default;
                iterator(CSVReader* reader) : daddy(reader) {};
                iterator(CSVReader*, CSVRow&&);

                reference operator*();
                pointer operator->();
                iterator& operator++(); // Pre-inc
                iterator operator++(int); // Post-inc
                iterator& operator--();

                bool operator==(const iterator&) const;
                bool operator!=(const iterator& other) const { return !operator==(other); }

            private:
                CSVReader * daddy = nullptr;  // Pointer to parent
                CSVRow row;                   // Current row
                RowCount i = 0;               // Index of current row
            };

            /** @name Constructors
             *  Constructors for iterating over large files and parsing in-memory sources.
             */
            ///@{
            CSVReader(const std::string& filename, CSVFormat format = GUESS_CSV);
            CSVReader(CSVFormat format = DEFAULT_CSV);
            ///@}

            CSVReader(const CSVReader&) = delete; // No copy constructor
            CSVReader(CSVReader&&) = default;     // Move constructor
            CSVReader& operator=(const CSVReader&) = delete; // No copy assignment
            CSVReader& operator=(CSVReader&& other) = default;
            ~CSVReader() { this->close(); }

            /** @name Reading In-Memory Strings
             *  You can piece together incomplete CSV fragments by calling feed() on them
             *  before finally calling end_feed().
             *
             *  Alternatively, you can also use the parse() shorthand function for
             *  smaller strings.
             */
            ///@{
            void feed(std::string_view in);
            void end_feed();
            ///@}

            /** @name Retrieving CSV Rows */
            ///@{
            bool read_row(CSVRow &row);
            iterator begin();
            iterator end();
            ///@}

            /** @name CSV Metadata */
            ///@{
            CSVFormat get_format() const;
            std::vector<std::string> get_col_names() const;
            int index_of(const std::string& col_name) const;
            ///@}

            /** @name CSV Metadata: Attributes */
            ///@{
            RowCount row_num = 0;        /**< @brief How many lines have
                                          *    been parsed so far
                                          */
            RowCount correct_rows = 0;   /**< @brief How many correct rows
                                          *    (minus header) have been parsed so far
                                          */
            bool utf8_bom = false;       /**< @brief Set to true if UTF-8 BOM was detected */
            ///@}

            void close();               /**< @brief Close the open file handle.
                                        *   Automatically called by ~CSVReader().
                                        */

            friend CSVCollection parse(const std::string&, CSVFormat);
        protected:
            /**
             * \defgroup csv_internal CSV Parser Internals
             * @brief Internals of CSVReader. Only maintainers and those looking to
             *        extend the parser should read this.
             * @{
             */

            /**  @typedef ParseFlags
             *   @brief   An enum used for describing the significance of each character
             *            with respect to CSV parsing
             */
            enum ParseFlags {
                NOT_SPECIAL,
                QUOTE,
                DELIMITER,
                NEWLINE
            };

            std::vector<CSVReader::ParseFlags> make_flags() const;

            internals::GiantStringBuffer record_buffer; /**<
                @brief Buffer for current row being parsed */

            std::vector<size_t> split_buffer; /**<
                @brief Positions where current row is split */

            std::deque<CSVRow> records; /**< @brief Queue of parsed CSV rows */
            inline bool eof() { return !(this->infile); };

            /** @name CSV Parsing Callbacks
             *  The heart of the CSV parser. 
             *  These methods are called by feed().
            */
            ///@{
            void write_record();
            virtual void bad_row_handler(std::vector<std::string>);
            ///@}

            /** @name CSV Settings **/
            ///@{
            char delimiter;                /**< @brief Delimiter character */
            char quote_char;               /**< @brief Quote character */
            int header_row;                /**< @brief Line number of the header row (zero-indexed) */
            bool strict = false;           /**< @brief Strictness of parser */

            std::vector<CSVReader::ParseFlags> parse_flags; /**< @brief
            A table where the (i + 128)th slot gives the ParseFlags for ASCII character i */
            ///@}

            /** @name Parser State */
            ///@{
            /** <@brief Pointer to a object containing column information
            */
            std::shared_ptr<internals::ColNames> col_names =
                std::make_shared<internals::ColNames>(std::vector<std::string>({}));

            /** <@brief Whether or not an attempt to find Unicode BOM has been made */
            bool unicode_bom_scan = false;
            ///@}

            /** @name Multi-Threaded File Reading Functions */
            ///@{
            void feed(std::unique_ptr<char[]>&&); /**< @brief Helper for read_csv_worker() */
            void read_csv(
                const std::string& filename,
                const size_t& bytes = ITERATION_CHUNK_SIZE,
                bool close = true
            );
            void read_csv_worker();
            ///@}

            /** @name Multi-Threaded File Reading: Flags and State */
            ///@{
            std::FILE* infile = nullptr;        /**< @brief Current file handle.
                                                     Destroyed by ~CSVReader(). */

            std::deque<std::unique_ptr<char[]>>
                feed_buffer;                    /**< @brief Message queue for worker */

            std::mutex feed_lock;               /**< @brief Allow only one worker to write */
            std::condition_variable feed_cond;  /**< @brief Wake up worker */
            ///@}

            /**@}*/ // End of parser internals
    };
    
    /** @class CSVStat
     *  @brief Class for calculating statistics from CSV files and in-memory sources
     *
     *  **Example**
     *  \include programs/csv_stats.cpp
     *
     */
    class CSVStat: public CSVReader {
        public:
            using FreqCount = std::unordered_map<std::string, RowCount>;
            using TypeCount = std::unordered_map<DataType, RowCount>;

            void end_feed();
            std::vector<long double> get_mean() const;
            std::vector<long double> get_variance() const;
            std::vector<long double> get_mins() const;
            std::vector<long double> get_maxes() const;
            std::vector<FreqCount> get_counts() const;
            std::vector<TypeCount> get_dtypes() const;

            CSVStat(std::string filename, CSVFormat format = GUESS_CSV);
            CSVStat(CSVFormat format = DEFAULT_CSV) : CSVReader(format) {};
        private:
            // An array of rolling averages
            // Each index corresponds to the rolling mean for the column at said index
            std::vector<long double> rolling_means;
            std::vector<long double> rolling_vars;
            std::vector<long double> mins;
            std::vector<long double> maxes;
            std::vector<FreqCount> counts;
            std::vector<TypeCount> dtypes;
            std::vector<long double> n;
            
            // Statistic calculators
            void variance(const long double&, const size_t&);
            void count(CSVField&, const size_t&);
            void min_max(const long double&, const size_t&);
            void dtype(CSVField&, const size_t&);

            void calc();
            void calc_worker(const size_t&);
    };

    namespace internals {
        /** Class for guessing the delimiter & header row number of CSV files */
        class CSVGuesser {
            struct Guesser : public CSVReader {
                using CSVReader::CSVReader;
                void bad_row_handler(std::vector<std::string> record) override;
                friend CSVGuesser;

                // Frequency counter of row length
                std::unordered_map<size_t, size_t> row_tally = { { 0, 0 } };

                // Map row lengths to row num where they first occurred
                std::unordered_map<size_t, size_t> row_when = { { 0, 0 } };
            };

        public:
            CSVGuesser(const std::string& _filename) : filename(_filename) {};
            std::vector<char> delims = { ',', '|', '\t', ';', '^' };
            void guess_delim();
            bool first_guess();
            void second_guess();

            char delim;
            int header_row = 0;

        private:
            std::string filename;
        };
    }

    /** @name Shorthand Parsing Functions
     *  @brief Convienience functions for parsing small strings
     */
    ///@{
    CSVCollection operator ""_csv(const char*, size_t);
    CSVCollection parse(const std::string& in, CSVFormat format = DEFAULT_CSV);
    ///@}

    /** @name Utility Functions */
    ///@{
    std::unordered_map<std::string, DataType> csv_data_types(const std::string&);
    CSVFileInfo get_file_info(const std::string& filename);
    CSVFormat guess_format(const std::string& filename);
    std::vector<std::string> get_col_names(
        const std::string& filename,
        const CSVFormat format = GUESS_CSV);
    int get_col_pos(const std::string filename, const std::string col_name,
        const CSVFormat format = GUESS_CSV);
    ///@}
 }
namespace csv { 
    namespace internals {
        bool is_equal(double a, double b, double epsilon) {
            /** Returns true if two doubles are about the same */
            return std::abs(a - b) < epsilon;
        }

        std::string format_row(const std::vector<std::string>& row, const std::string& delim) {
            /** Print a CSV row */
            std::stringstream ret;
            for (size_t i = 0; i < row.size(); i++) {
                ret << row[i];
                if (i + 1 < row.size()) ret << delim;
                else ret << std::endl;
            }

            return ret.str();
        }

        //
        // GiantStringBuffer
        //
        std::string_view GiantStringBuffer::get_row() {
            /**
             * Return a string_view over the current_row
             */
                        
            std::string_view ret(
                this->buffer->c_str() + this->current_end, // Beginning of string
                (this->buffer->size() - this->current_end) // Count
            );

            this->current_end = this->buffer->size();
            return ret;
        }

        void GiantStringBuffer::operator+=(const char ch) {
            *(this->buffer) += ch;
        }

        size_t GiantStringBuffer::size() const {
            // Return size of current row
            return (this->buffer->size() - this->current_end);
        }

        std::string* GiantStringBuffer::get() {
            return this->buffer.get();
        }

        std::string* GiantStringBuffer::operator->() {
            if (!this->buffer)
                this->buffer = std::make_shared<std::string>();

            return this->buffer.operator->();
        }

        void GiantStringBuffer::reset() {
            auto temp_str = this->buffer->substr(
                this->current_end,   // Position
                (this->buffer->size() - this->current_end) // Count
            );

            this->current_end = 0;
            this->buffer = std::make_shared<std::string>(
                temp_str
            );
        }

        //
        // CSVGuesser
        //
        void CSVGuesser::Guesser::bad_row_handler(std::vector<std::string> record) {
            /** Helps CSVGuesser tally up the size of rows encountered while parsing */
            if (row_tally.find(record.size()) != row_tally.end()) row_tally[record.size()]++;
            else {
                row_tally[record.size()] = 1;
                row_when[record.size()] = this->row_num + 1;
            }
        }

        void CSVGuesser::guess_delim() {
            /** Guess the delimiter of a CSV by scanning the first 100 lines by
            *  First assuming that the header is on the first row
            *  If the first guess returns too few rows, then we move to the second
            *  guess method
            */
            if (!first_guess()) second_guess();
        }

        bool CSVGuesser::first_guess() {
            /** Guess the delimiter of a delimiter separated values file
            *  by scanning the first 100 lines
            *
            *  - "Winner" is based on which delimiter has the most number
            *    of correctly parsed rows + largest number of columns
            *  -  **Note:** Assumes that whatever the dialect, all records
            *     are newline separated
            *
            *  Returns True if guess was a good one and second guess isn't needed
            */

            CSVFormat format = DEFAULT_CSV;
            char current_delim{ ',' };
            RowCount max_rows = 0;
            RowCount temp_rows = 0;
            size_t max_cols = 0;

            for (size_t i = 0; i < delims.size(); i++) {
                format.delim = delims[i];
                CSVReader guesser(this->filename, format);

                // WORKAROUND on Unix systems because certain newlines
                // get double counted
                // temp_rows = guesser.correct_rows;
                temp_rows = std::min(guesser.correct_rows, (RowCount)100);
                if ((guesser.row_num >= max_rows) &&
                    (guesser.get_col_names().size() > max_cols)) {
                    max_rows = temp_rows;
                    max_cols = guesser.get_col_names().size();
                    current_delim = delims[i];
                }
            }

            this->delim = current_delim;

            // If there are only a few rows/columns, trying guessing again
            return (max_rows > 10 && max_cols > 2);
        }

        void CSVGuesser::second_guess() {
            /** For each delimiter, find out which row length was most common.
            *  The delimiter with the longest mode row length wins.
            *  Then, the line number of the header row is the first row with
            *  the mode row length.
            */

            CSVFormat format = DEFAULT_CSV;
            size_t max_rlen = 0;
            size_t header = 0;

            for (auto it = delims.begin(); it != delims.end(); ++it) {
                format.delim = *it;
                Guesser guess(format);
                guess.read_csv(filename, 500000);

                // Most common row length
                auto max = std::max_element(guess.row_tally.begin(), guess.row_tally.end(),
                    [](const std::pair<size_t, size_t>& x,
                        const std::pair<size_t, size_t>& y) {
                    return x.second < y.second; });

                // Idea: If CSV has leading comments, actual rows don't start
                // until later and all actual rows get rejected because the CSV
                // parser mistakenly uses the .size() of the comment rows to
                // judge whether or not they are valid.
                // 
                // The first part of the if clause means we only change the header
                // row if (number of rejected rows) > (number of actual rows)
                if (max->second > guess.records.size() &&
                    (max->first > max_rlen)) {
                    max_rlen = max->first;
                    header = guess.row_when[max_rlen];
                }
            }

            this->header_row = static_cast<int>(header);
        }
    }

    /** @brief Guess the delimiter used by a delimiter-separated values file */
    CSVFormat guess_format(const std::string& filename) {
        internals::CSVGuesser guesser(filename);
        guesser.guess_delim();
        return { guesser.delim, '"', guesser.header_row };
    }

    std::vector<CSVReader::ParseFlags> CSVReader::make_flags() const {
        /** Create a vector v where each index i corresponds to the
         *  ASCII number for a character and, v[i + 128] labels it according to
         *  the CSVReader::ParseFlags enum
         */

        std::vector<ParseFlags> ret;
        for (int i = -128; i < 128; i++) {
            char ch = char(i);

            if (ch == this->delimiter)
                ret.push_back(DELIMITER);
            else if (ch == this->quote_char)
                ret.push_back(QUOTE);
            else if (ch == '\r' || ch == '\n')
                ret.push_back(NEWLINE);
            else
                ret.push_back(NOT_SPECIAL);
        }

        return ret;
    }

    void CSVReader::bad_row_handler(std::vector<std::string> record) {
        /** Handler for rejected rows (too short or too long). This does nothing
         *  unless strict parsing was set, in which case it throws an eror.
         *  Subclasses of CSVReader may easily override this to provide
         *  custom behavior.
         */
        if (this->strict) {
            std::string problem;
            if (record.size() > this->col_names->size()) problem = "too long";
            else problem = "too short";

            throw std::runtime_error("Line " + problem + " around line " +
                std::to_string(correct_rows) + " near\n" +
                internals::format_row(record)
            );
        }
    };

    /**
     *  @brief Shorthand function for parsing an in-memory CSV string,
     *  a collection of CSVRow objects
     *
     *  \snippet tests/test_read_csv.cpp Parse Example
     *
     */
    CSVCollection parse(const std::string& in, CSVFormat format) {
        CSVReader parser(format);
        parser.feed(in);
        parser.end_feed();
        return parser.records;
    }

    /** 
     * @brief Parse a RFC 4180 CSV string, returning a collection
     *        of CSVRow objects
     *
     * **Example:**
     *  \snippet tests/test_read_csv.cpp Escaped Comma
     *
     */
    CSVCollection operator ""_csv(const char* in, size_t n) {    
        std::string temp(in, n);
        return parse(temp);
    }

    /**
     *  @brief Return a CSV's column names
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     */
    std::vector<std::string> get_col_names(const std::string& filename, CSVFormat format) {
        CSVReader reader(filename, format);
        return reader.get_col_names();
    }

    /**
     *  @brief Find the position of a column in a CSV file or CSV_NOT_FOUND otherwise
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] col_name  Column whose position we should resolve
     *  @param[in] format    Format of the CSV file
     */
    int get_col_pos(
        const std::string filename,
        const std::string col_name,
        const CSVFormat format) {
        CSVReader reader(filename, format);
        return reader.index_of(col_name);
    }

    /** @brief Get basic information about a CSV file
     *  \include programs/csv_info.cpp
     */
    CSVFileInfo get_file_info(const std::string& filename) {
        CSVReader reader(filename);
        CSVFormat format = reader.get_format();
        for (auto& row: reader);

        CSVFileInfo info = {
            filename,
            reader.get_col_names(),
            format.delim,
            reader.correct_rows,
            (int)reader.get_col_names().size()
        };

        return info;
    }

    /**
     *  @brief Allows parsing in-memory sources (by calling feed() and end_feed()).
     */
    CSVReader::CSVReader(CSVFormat format) :
        delimiter(format.delim), quote_char(format.quote_char),
        header_row(format.header), strict(format.strict),
        unicode_bom_scan(!format.unicode_detect) {
        if (!format.col_names.empty()) {
            this->header_row = -1;
            this->col_names = std::make_shared<internals::ColNames>(format.col_names);
        }
    };

    /**
     *  @brief Allows reading a CSV file in chunks, using overlapped
     *         threads for simulatenously reading from disk and parsing.
     *         Rows should be retrieved with read_row() or by using
     *         CSVReader::iterator.
     *
     * **Details:** Reads the first 500kB of a CSV file to infer file information
     *              such as column names and delimiting character.
     *
     *  @param[in] filename  Path to CSV file
     *  @param[in] format    Format of the CSV file
     *
     *  \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
    CSVReader::CSVReader(const std::string& filename, CSVFormat format) {
        if (format.delim == '\0')
            format = guess_format(filename);

        this->col_names = std::make_shared<internals::ColNames>(format.col_names);
        delimiter = format.delim;
        quote_char = format.quote_char;
        header_row = format.header;
        strict = format.strict;

        // Read first 500KB of CSV
        read_csv(filename, 500000, false);
    }

    /** @brief Return the format of the original raw CSV */
    CSVFormat CSVReader::get_format() const {
        return {
            this->delimiter,
            this->quote_char,
            this->header_row,
            this->col_names->col_names
        };
    }

    /** @brief Return the CSV's column names as a vector of strings. */
    std::vector<std::string> CSVReader::get_col_names() const {
        return this->col_names->get_col_names();
    }

    /** @brief Return the index of the column name if found or
     *         csv::CSV_NOT_FOUND otherwise.
     */
    int CSVReader::index_of(const std::string& col_name) const {
        auto col_names = this->get_col_names();
        for (size_t i = 0; i < col_names.size(); i++)
            if (col_names[i] == col_name) return (int)i;

        return CSV_NOT_FOUND;
    }

    void CSVReader::feed(std::unique_ptr<char[]>&& buff) {
        this->feed(std::string_view(buff.get()));
    }

    void CSVReader::feed(std::string_view in) {
        /** @brief Parse a CSV-formatted string.
         *
         *  Incomplete CSV fragments can be joined together by calling feed() on them sequentially.
         *  **Note**: end_feed() should be called after the last string
         */

        if (parse_flags.empty()) parse_flags = this->make_flags();

        bool quote_escape = false;  // Are we currently in a quote escaped field?

        // Unicode BOM Handling
        if (!this->unicode_bom_scan) {
            if (in[0] == 0xEF && in[1] == 0xBB && in[2] == 0xEF) {
                in.remove_prefix(3); // Remove BOM from input string
                this->utf8_bom = true;
            }

            this->unicode_bom_scan = true;
        }

        // Optimization
        this->record_buffer->reserve(in.size());
        std::string& _record_buffer = *(this->record_buffer.get());

        for (size_t i = 0; i < in.size(); i++) {
            if (!quote_escape) {
                switch (this->parse_flags[in[i] + 128]) {
                case NOT_SPECIAL:
                    _record_buffer +=in[i];
                    break;
                case DELIMITER:
                    this->split_buffer.push_back(this->record_buffer.size());
                    break;
                case NEWLINE:
                    // End of record -> Write record
                    if (i + 1 < in.size() && in[i + 1] == '\n') // Catches CRLF (or LFLF)
                        ++i;
                    this->write_record();
                    break;
                default: // Quote
                    // Case: Previous character was delimiter or newline
                    if (i) { // Don't deref past beginning
                        auto prev_ch = this->parse_flags[in[i - 1] + 128];
                        if (prev_ch >= DELIMITER) quote_escape = true;
                    }
                    break;
                }
            }
            else {
                switch (this->parse_flags[in[i] + 128]) {
                case NOT_SPECIAL:
                case DELIMITER:
                case NEWLINE:
                    // Treat as a regular character
                    _record_buffer +=in[i];
                    break;
                default: // Quote
                    auto next_ch = this->parse_flags[in[i + 1] + 128];
                    if (next_ch >= DELIMITER) {
                        // Case: Delim or newline => end of field
                        quote_escape = false;
                    }
                    else {
                        // Case: Escaped quote
                        _record_buffer +=in[i];

                        if (next_ch == QUOTE)
                            ++i;  // Case: Two consecutive quotes
                        else if (this->strict)
                            throw std::runtime_error("Unescaped single quote around line " +
                                std::to_string(this->correct_rows) + " near:\n" +
                                std::string(in.substr(i, 100)));
                    }
                }
            }
        }

        this->record_buffer.reset();
    }

    void CSVReader::end_feed() {
        /** Indicate that there is no more data to receive,
        *  and handle the last row
        */
        this->write_record();
    }

    void CSVReader::write_record() {
        /** Push the current row into a queue if it is the right length.
         *  Drop it otherwise.
         */

        size_t col_names_size = this->col_names->size();

        auto row = CSVRow(
            this->record_buffer.buffer,
            this->record_buffer.get_row(),
            std::move(this->split_buffer),
            this->col_names
        );

        if (this->row_num > this->header_row) {
            // Make sure record is of the right length
            if (row.size() == col_names_size) {
                this->correct_rows++;
                this->records.push_back(std::move(row));
            }
            else {
                /* 1) Zero-length record, probably caused by extraneous newlines
                 * 2) Too short or too long
                 */
                this->row_num--;
                if (!row.empty())
                    bad_row_handler(std::vector<std::string>(row));
            }
        }
        else if (this->row_num == this->header_row) {
            this->col_names = std::make_shared<internals::ColNames>(
                std::vector<std::string>(row));
        } // else: Ignore rows before header row

        // Some memory allocation optimizations
        this->split_buffer = {};
        if (this->split_buffer.capacity() < col_names_size)
            split_buffer.reserve(col_names_size);

        this->row_num++;
    }

    void CSVReader::read_csv_worker() {
        /** @brief Worker thread for read_csv() which parses CSV rows (while the main
         *         thread pulls data from disk)
         */
        while (true) {
            std::unique_lock<std::mutex> lock{ this->feed_lock }; // Get lock
            this->feed_cond.wait(lock,                            // Wait
                [this] { return !(this->feed_buffer.empty()); });

            // Wake-up
            auto in = std::move(this->feed_buffer.front());
            this->feed_buffer.pop_front();

            // Nullptr --> Die
            if (!in) break;

            lock.unlock();      // Release lock
            this->feed(std::move(in));
        }
    }

    /**
     * @brief Parse a CSV file using multiple threads
     *
     * @param[in] nrows Number of rows to read. Set to -1 to read entire file.
     * @param[in] close Close file after reading?
     *
     * @see CSVReader::read_row()
     * 
     */
    void CSVReader::read_csv(const std::string& filename, const size_t& bytes, bool close) {
        if (!this->infile) {
            #ifdef _MSC_BUILD
            // Silence compiler warnings in Microsoft Visual C++
            size_t err = fopen_s(&(this->infile), filename.c_str(), "rb");
            if (err)
                throw std::runtime_error("Cannot open file " + filename);
            #else
            this->infile = std::fopen(filename.c_str(), "rb");
            if (!this->infile)
                throw std::runtime_error("Cannot open file " + filename);
            #endif
        }

        const size_t BUFFER_UPPER_LIMIT = std::min(bytes, (size_t)1000000);
        std::unique_ptr<char[]> buffer(new char[BUFFER_UPPER_LIMIT]);
        auto line_buffer = buffer.get();
        std::thread worker(&CSVReader::read_csv_worker, this);

        for (size_t processed = 0; processed < bytes; ) {
            char * result = std::fgets(line_buffer, PAGE_SIZE, this->infile);
            if (result == NULL) break;
            line_buffer += std::strlen(line_buffer);

            if ((line_buffer - buffer.get()) >= 0.9 * BUFFER_UPPER_LIMIT) {
                processed += (line_buffer - buffer.get());
                std::unique_lock<std::mutex> lock{ this->feed_lock };
                this->feed_buffer.push_back(std::move(buffer));
                this->feed_cond.notify_one();

                buffer = std::make_unique<char[]>(BUFFER_UPPER_LIMIT); // New pointer
                line_buffer = buffer.get();
            }
        }

        // Feed remaining bits
        std::unique_lock<std::mutex> lock{ this->feed_lock };
        this->feed_buffer.push_back(std::move(buffer));
        this->feed_buffer.push_back(nullptr); // Termination signal
        this->feed_cond.notify_one();
        lock.unlock();
        worker.join();

        if (std::feof(this->infile)) {
            this->end_feed();
            this->close();
        }
    }

    void CSVReader::close() {
        if (this->infile) {
            std::fclose(this->infile);
            this->infile = nullptr;
        }
    }

    /**
     * @brief Retrieve rows as CSVRow objects, returning true if more rows are available.
     *
     * **Performance Notes**:
     *  - The number of rows read in at a time is determined by csv::ITERATION_CHUNK_SIZE
     *  - For performance details, read the documentation for CSVRow and CSVField.
     *
     * @param[out] row The variable where the parsed row will be stored
     * @see CSVRow, CSVField
     *
     * **Example:**
     * \snippet tests/test_read_csv.cpp CSVField Example
     *
     */
    bool CSVReader::read_row(CSVRow &row) {
        if (this->records.empty()) {
            if (!this->eof()) {
                this->read_csv("", ITERATION_CHUNK_SIZE, false);
            }
            else return false; // Stop reading
        }

        row = std::move(this->records.front());
        this->records.pop_front();

        return true;
    }

    /**
     * @brief Return an iterator to the first row in the reader
     *
     */
    CSVReader::iterator CSVReader::begin() {
        CSVReader::iterator ret(this, std::move(this->records.front()));
        this->records.pop_front();
        return ret;
    }

    /**
     * @brief A placeholder for the imaginary past the end row in a CSV.
     *        Attempting to deference this will lead to bad things.
     */
    CSVReader::iterator CSVReader::end() {
        return CSVReader::iterator();
    }

    /////////////////////////
    // CSVReader::iterator //
    /////////////////////////

    CSVReader::iterator::iterator(CSVReader* _daddy, CSVRow&& _row) :
        daddy(_daddy) {
        row = std::move(_row);
    }

    /** @brief Access the CSVRow held by the iterator */
    CSVReader::iterator::reference CSVReader::iterator::operator*() {
        return this->row;
    }

    /** @brief Return a pointer to the CSVRow the iterator has stopped at */
    CSVReader::iterator::pointer CSVReader::iterator::operator->() {
        return &(this->row);
    }

    /** @brief Advance the iterator by one row. If this CSVReader has an
     *  associated file, then the iterator will lazily pull more data from
     *  that file until EOF.
     */
    CSVReader::iterator& CSVReader::iterator::operator++() {
        if (!daddy->read_row(this->row)) {
            this->daddy = nullptr; // this == end()
        }

        return *this;
    }

    /** @brief Post-increment iterator */
    CSVReader::iterator CSVReader::iterator::operator++(int) {
        auto temp = *this;
        if (!daddy->read_row(this->row)) {
            this->daddy = nullptr; // this == end()
        }

        return temp;
    }

    /** @brief Returns true if iterators were constructed from the same CSVReader
     *         and point to the same row
     */
    bool CSVReader::iterator::operator==(const CSVReader::iterator& other) const {
        return (this->daddy == other.daddy) && (this->i == other.i);
    }

    namespace internals {
        //////////////
        // ColNames //
        //////////////

        ColNames::ColNames(const std::vector<std::string>& _cnames)
            : col_names(_cnames) {
            for (size_t i = 0; i < _cnames.size(); i++) {
                this->col_pos[_cnames[i]] = i;
            }
        }

        std::vector<std::string> ColNames::get_col_names() const {
            return this->col_names;
        }

        size_t ColNames::size() const {
            return this->col_names.size();
        }
    }

    /** @brief Return the number of fields in this row */
    size_t CSVRow::size() const {
        return splits.size() + 1;
    }

    /** @brief      Return a string view of the nth field
     *  @complexity Constant
     */
    std::string_view CSVRow::get_string_view(size_t n) const {
        std::string_view ret(this->row_str);
        size_t beg = 0, end = row_str.size(),
            r_size = this->size();

        if (n >= r_size)
            throw std::runtime_error("Index out of bounds.");

        if (!splits.empty()) {
            if (n == 0 || r_size == 2) {
                if (n == 0) end = this->splits[0];
                else beg = this->splits[0];
            }
            else {
                beg = this->splits[n - 1];
                if (n != r_size - 1) end = this->splits[n];
            }
        }
        
        return ret.substr(
            beg,
            end - beg // Number of characters
        );
    }

    /** @brief Return a CSVField object corrsponding to the nth value in the row.
     *
     *  This method performs boounds checking, and will throw an std::runtime_error
     *  if n is invalid.
     *
     *  @complexity Constant, by calling CSVRow::get_string_view()
     *
     */
    CSVField CSVRow::operator[](size_t n) const {
        return CSVField(this->get_string_view(n));
    }

    /** @brief Retrieve a value by its associated column name. If the column
     *         specified can't be round, a runtime error is thrown.
     *
     *  @complexity Constant. This calls the other CSVRow::operator[]() after
                    converting column names into indices using a hash table.
     *
     *  @param[in] col_name The column to look for
     */
    CSVField CSVRow::operator[](const std::string& col_name) const {
        auto col_pos = this->col_names->col_pos.find(col_name);
        if (col_pos != this->col_names->col_pos.end())
            return this->operator[](col_pos->second);

        throw std::runtime_error("Can't find a column named " + col_name);
    }

    CSVRow::operator std::vector<std::string>() const {
        /** Convert this CSVRow into a vector of strings.
         *  **Note**: This is a less efficient method of
         *  accessing data than using the [] operator.
         */

        std::vector<std::string> ret;
        for (size_t i = 0; i < size(); i++)
            ret.push_back(std::string(this->get_string_view(i)));

        return ret;
    }

    //////////////////////
    // CSVField Methods //
    //////////////////////

    /**< @brief Return the type number of the stored value in
     *          accordance with the DataType enum
     */
    DataType CSVField::type() {
        this->get_value();
        return (DataType)_type;
    }

    #ifndef DOXYGEN_SHOULD_SKIP_THIS
    void CSVField::get_value() {
        /* Check to see if value has been cached previously, if not
         * evaluate it
         */
        if (_type < 0) {
            auto dtype = internals::data_type(this->sv, &this->value);
            this->_type = (int)dtype;
        }
    }
    #endif

    //
    // CSVField Utility Methods
    //

    bool CSVField::operator==(std::string_view other) const {
        return other == this->sv;
    }

    bool CSVField::operator==(const long double& other) {
        return other == this->get<long double>();
    }

    /////////////////////
    // CSVRow Iterator //
    /////////////////////

    /** @brief Return an iterator pointing to the first field. */
    CSVRow::iterator CSVRow::begin() const {
        return CSVRow::iterator(this, 0);
    }

    /** @brief Return an iterator pointing to just after the end of the CSVRow.
     *
     *  Attempting to dereference the end iterator results in undefined behavior.
     */
    CSVRow::iterator CSVRow::end() const {
        return CSVRow::iterator(this, (int)this->size());
    }

    CSVRow::reverse_iterator CSVRow::rbegin() const {
        return std::make_reverse_iterator<CSVRow::iterator>(this->end());
    }

    CSVRow::reverse_iterator CSVRow::rend() const {
        return std::make_reverse_iterator<CSVRow::iterator>(this->begin());
    }

    CSVRow::iterator::iterator(const CSVRow* _reader, int _i)
        : daddy(_reader), i(_i) {
        if (_i < this->daddy->size())
            this->field = std::make_shared<CSVField>(
                this->daddy->operator[](_i));
        else
            this->field = nullptr;
    }

    CSVRow::iterator::reference CSVRow::iterator::operator*() const {
        return *(this->field.get());
    }

    CSVRow::iterator::pointer CSVRow::iterator::operator->() const {
        return this->field.get();
    }

    CSVRow::iterator& CSVRow::iterator::operator++() {
        // Pre-increment operator
        this->i++;
        if (this->i < this->daddy->size())
            this->field = std::make_shared<CSVField>(
                this->daddy->operator[](i));
        else // Reached the end of row
            this->field = nullptr;
        return *this;
    }

    CSVRow::iterator CSVRow::iterator::operator++(int) {
        // Post-increment operator
        auto temp = *this;
        this->operator++();
        return temp;
    }

    CSVRow::iterator& CSVRow::iterator::operator--() {
        // Pre-decrement operator
        this->i--;
        this->field = std::make_shared<CSVField>(
            this->daddy->operator[](this->i));
        return *this;
    }

    CSVRow::iterator CSVRow::iterator::operator--(int) {
        // Post-decrement operator
        auto temp = *this;
        this->operator--();
        return temp;
    }
    
    CSVRow::iterator CSVRow::iterator::operator+(difference_type n) const {
        // Allows for iterator arithmetic
        return CSVRow::iterator(this->daddy, i + (int)n);
    }

    CSVRow::iterator CSVRow::iterator::operator-(difference_type n) const {
        // Allows for iterator arithmetic
        return CSVRow::iterator::operator+(-n);
    }

    /** @brief Two iterators are equal if they point to the same field */
    bool CSVRow::iterator::operator==(const iterator& other) const {
        return this->i == other.i;
    }

    /** @file
      * Calculates statistics from CSV files
      */

    CSVStat::CSVStat(std::string filename, CSVFormat format) :
        CSVReader(filename, format) {
        /** Lazily calculate statistics for a potentially large file. Once this constructor
         *  is called, CSVStat will process the entire file iteratively. Once finished,
         *  methods like get_mean(), get_counts(), etc... can be used to retrieve statistics.
         */
        while (!this->eof()) {
            this->read_csv("", ITERATION_CHUNK_SIZE, false);
            this->calc();
        }

        if (!this->records.empty())
            this->calc();
    }

    void CSVStat::end_feed() {
        CSVReader::end_feed();
        this->calc();
    }

    /** @brief Return current means */
    std::vector<long double> CSVStat::get_mean() const {
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->rolling_means[i]);
        }
        return ret;
    }

    /** @brief Return current variances */
    std::vector<long double> CSVStat::get_variance() const {
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->rolling_vars[i]/(this->n[i] - 1));
        }
        return ret;
    }

    /** @brief Return current mins */
    std::vector<long double> CSVStat::get_mins() const {
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->mins[i]);
        }
        return ret;
    }

    /** @brief Return current maxes */
    std::vector<long double> CSVStat::get_maxes() const {
        std::vector<long double> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->maxes[i]);
        }
        return ret;
    }

    /** @brief Get counts for each column */
    std::vector<CSVStat::FreqCount> CSVStat::get_counts() const {
        std::vector<FreqCount> ret;
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->counts[i]);
        }
        return ret;
    }

    /** @brief Get data type counts for each column */
    std::vector<CSVStat::TypeCount> CSVStat::get_dtypes() const {
        std::vector<TypeCount> ret;        
        for (size_t i = 0; i < this->col_names->size(); i++) {
            ret.push_back(this->dtypes[i]);
        }
        return ret;
    }

    void CSVStat::calc() {
        /** Go through all records and calculate specified statistics */
        for (size_t i = 0; i < this->col_names->size(); i++) {
            dtypes.push_back({});
            counts.push_back({});
            rolling_means.push_back(0);
            rolling_vars.push_back(0);
            mins.push_back(NAN);
            maxes.push_back(NAN);
            n.push_back(0);
        }

        std::vector<std::thread> pool;

        // Start threads
        for (size_t i = 0; i < this->col_names->size(); i++)
            pool.push_back(std::thread(&CSVStat::calc_worker, this, i));

        // Block until done
        for (auto& th: pool)
            th.join();

        this->records.clear();
    }

    void CSVStat::calc_worker(const size_t &i) {
        /** Worker thread for CSVStat::calc() which calculates statistics for one column.
         * 
         *  @param[in] i Column index
         */

        auto current_record = this->records.begin();
        for (size_t processed = 0; current_record != this->records.end(); processed++) {
            auto current_field = (*current_record)[i];

            // Optimization: Don't count() if there's too many distinct values in the first 1000 rows
            if (processed < 1000 || this->counts[i].size() <= 500)
                this->count(current_field, i);

            this->dtype(current_field, i);

            // Numeric Stuff
            if (current_field.type() >= CSV_INT) {
                long double x_n = current_field.get<long double>();

                // This actually calculates mean AND variance
                this->variance(x_n, i);
                this->min_max(x_n, i);
            }

            ++current_record;
        }
    }

    void CSVStat::dtype(CSVField& data, const size_t &i) {
        /** Given a record update the type counter
         *  @param[in]  record Data observation
         *  @param[out] i      The column index that should be updated
         */
        
        auto type = data.type();
        if (this->dtypes[i].find(type) !=
            this->dtypes[i].end()) {
            // Increment count
            this->dtypes[i][type]++;
        } else {
            // Initialize count
            this->dtypes[i].insert(std::make_pair(type, 1));
        }
    }

    void CSVStat::count(CSVField& data, const size_t &i) {
        /** Given a record update the frequency counter
         *  @param[in]  record Data observation
         *  @param[out] i      The column index that should be updated
         */

        auto item = data.get<std::string>();

        if (this->counts[i].find(item) !=
            this->counts[i].end()) {
            // Increment count
            this->counts[i][item]++;
        } else {
            // Initialize count
            this->counts[i].insert(std::make_pair(item, 1));
        }
    }

    void CSVStat::min_max(const long double &x_n, const size_t &i) {
        /** Update current minimum and maximum
         *  @param[in]  x_n Data observation
         *  @param[out] i   The column index that should be updated
         */
        if (isnan(this->mins[i]))
            this->mins[i] = x_n;
        if (isnan(this->maxes[i]))
            this->maxes[i] = x_n;
        
        if (x_n < this->mins[i])
            this->mins[i] = x_n;
        else if (x_n > this->maxes[i])
            this->maxes[i] = x_n;
    }

    void CSVStat::variance(const long double &x_n, const size_t &i) {
        /** Given a record update rolling mean and variance for all columns
         *  using Welford's Algorithm
         *  @param[in]  x_n Data observation
         *  @param[out] i   The column index that should be updated
         */
        long double& current_rolling_mean = this->rolling_means[i];
        long double& current_rolling_var = this->rolling_vars[i];
        long double& current_n = this->n[i];
        long double delta;
        long double delta2;

        current_n++;
        
        if (current_n == 1) {
            current_rolling_mean = x_n;
        } else {
            delta = x_n - current_rolling_mean;
            current_rolling_mean += delta/current_n;
            delta2 = x_n - current_rolling_mean;
            current_rolling_var += delta*delta2;
        }
    }

    /** @brief Useful for uploading CSV files to SQL databases.
     *
     *  Return a data type for each column such that every value in a column can be
     *  converted to the corresponding data type without data loss.
     *  @param[in]  filename The CSV file
     *
     *  \return A mapping of column names to csv::DataType enums
     */
    std::unordered_map<std::string, DataType> csv_data_types(const std::string& filename) {
        CSVStat stat(filename);
        std::unordered_map<std::string, DataType> csv_dtypes;

        auto col_names = stat.get_col_names();
        auto temp = stat.get_dtypes();

        for (size_t i = 0; i < stat.get_col_names().size(); i++) {
            auto& col = temp[i];
            auto& col_name = col_names[i];

            if (col[CSV_STRING])
                csv_dtypes[col_name] = CSV_STRING;
            else if (col[CSV_LONG_LONG_INT])
                csv_dtypes[col_name] = CSV_LONG_LONG_INT;
            else if (col[CSV_LONG_INT])
                csv_dtypes[col_name] = CSV_LONG_INT;
            else if (col[CSV_INT])
                csv_dtypes[col_name] = CSV_INT;
            else
                csv_dtypes[col_name] = CSV_DOUBLE;
        }

        return csv_dtypes;
    }
 }
namespace csv::internals { 
    #ifndef DOXYGEN_SHOULD_SKIP_THIS
    std::string type_name(const DataType& dtype) {
        switch (dtype) {
        case CSV_STRING:
            return "string";
        case CSV_INT:
            return "int";
        case CSV_LONG_INT:
            return "long int";
        case CSV_LONG_LONG_INT:
            return "long long int";
        case CSV_DOUBLE:
            return "double";
        default:
            return "null";
        }
    };
    #endif

    const long double _INT_MAX = (long double)std::numeric_limits<int>::max();
    const long double _LONG_MAX = (long double)std::numeric_limits<long int>::max();
    const long double _LONG_LONG_MAX = (long double)std::numeric_limits<long long int>::max();

    DataType data_type(std::string_view in, long double* const out) {
        /** Distinguishes numeric from other text values. Used by various
        *  type casting functions, like csv_parser::CSVReader::read_row()
        *
        *  #### Rules
        *   - Leading and trailing whitespace ("padding") ignored
        *   - A string of just whitespace is NULL
        *
        *  @param[in] in String value to be examined
        */

        // Empty string --> NULL
        if (in.size() == 0)
            return CSV_NULL;

        bool ws_allowed = true;
        bool neg_allowed = true;
        bool dot_allowed = true;
        bool digit_allowed = true;
        bool has_digit = false;
        bool prob_float = false;

        unsigned places_after_decimal = 0;
        long double integral_part = 0,
            decimal_part = 0;

        for (size_t i = 0, ilen = in.size(); i < ilen; i++) {
            const char& current = in[i];

            switch (current) {
            case ' ':
                if (!ws_allowed) {
                    if (isdigit(in[i - 1])) {
                        digit_allowed = false;
                        ws_allowed = true;
                    }
                    else {
                        // Ex: '510 123 4567'
                        return CSV_STRING;
                    }
                }
                break;
            case '-':
                if (!neg_allowed) {
                    // Ex: '510-123-4567'
                    return CSV_STRING;
                }
                
                neg_allowed = false;
                break;
            case '.':
                if (!dot_allowed) {
                    return CSV_STRING;
                }

                dot_allowed = false;
                prob_float = true;
                break;
            default:
                if (isdigit(current)) {
                    // Process digit
                    has_digit = true;

                    if (!digit_allowed)
                        return CSV_STRING;
                    else if (ws_allowed) // Ex: '510 456'
                        ws_allowed = false;

                    // Build current number
                    unsigned digit = current - '0';
                    if (prob_float) {
                        places_after_decimal++;
                        decimal_part = (decimal_part * 10) + digit;
                    }
                    else {
                        integral_part = (integral_part * 10) + digit;
                    }
                }
                else {
                    return CSV_STRING;
                }
            }
        }

        // No non-numeric/non-whitespace characters found
        if (has_digit) {
            long double number = integral_part + decimal_part * pow(10, -(double)places_after_decimal);
            if (out) *out = neg_allowed ? number : -number;

            if (prob_float)
                return CSV_DOUBLE;

            // We can assume number is always positive
            if (number < _INT_MAX)
                return CSV_INT;
            else if (number < _LONG_MAX)
                return CSV_LONG_INT;
            else if (number < _LONG_LONG_MAX)
                return CSV_LONG_LONG_INT;
            else // Conversion to long long will cause an overflow
                return CSV_DOUBLE;
        }

        // Just whitespace
        return CSV_NULL;
    }
 }
