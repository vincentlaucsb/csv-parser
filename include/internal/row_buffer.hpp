/** @file
 *  Defines an object which can store CSV data in
 *  continuous regions of memory
 */

#pragma once
#include <memory>
#include <vector>
#include <unordered_map>
#include <string>

#include "compatibility.hpp" // For string view

namespace csv {
    namespace internals {
        class RawRowBuffer;
        struct ColumnPositions;
        struct ColNames;
        using BufferPtr = std::shared_ptr<RawRowBuffer>;
        using ColNamesPtr = std::shared_ptr<ColNames>;
        using SplitArray = std::vector<unsigned short>;

        /** @struct ColNames
         *  A data structure for handling column name information.
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

        /** Class for reducing number of new string and new vector
         *  and malloc calls
         *
         *  @par Motivation
         *  By storing CSV strings in a giant string (as opposed to an
         *  `std::vector` of smaller strings), we vastly reduce the number
         *  of calls to `malloc()`, thus speeding up the program.
         *  However, by doing so we will need a way to tell where different
         *  fields are located within this giant string.
         *  Hence, an array of indices is also maintained.
         *
         *  @warning
         *  `reset()` should be called somewhat often in the code. Since each
         *  `csv::CSVRow` contains an `std::shared_ptr` to a RawRowBuffer,
         *  the buffers do not get deleted until every CSVRow referencing it gets
         *  deleted. If RawRowBuffers get very large, then so will memory consumption.
         *  Currently, `reset()` is called by `csv::CSVReader::feed()` at the end of 
         *  every sequence of bytes parsed.
         *  
         */
        class RawRowBuffer {
        public:
            RawRowBuffer() = default;

            /** Constructor mainly used for testing
             *  @param[in] _buffer    CSV text without delimiters or newlines
             *  @param[in] _splits    Positions in buffer where CSV fields begin
             *  @param[in] _col_names Pointer to a vector of column names
             */
            RawRowBuffer(const std::string& _buffer, const std::vector<unsigned short>& _splits,
                const std::shared_ptr<ColNames>& _col_names) :
                buffer(_buffer), split_buffer(_splits), col_names(_col_names) {};

            csv::string_view get_row();      /**< Return a string_view over the current_row */
            ColumnPositions get_splits();    /**< Return the field start positions for the current row */

            size_t size() const;             /**< Return size of current row */
            size_t splits_size() const;      /**< Return (num columns - 1) for current row */
            BufferPtr reset() const;         /**< Create a new RawRowBuffer with this buffer's unfinished work */

            std::string buffer;              /**< Buffer for storing text */
            SplitArray split_buffer = {};    /**< Array for storing indices (in buffer)
                                                  of where CSV fields start */
            ColNamesPtr col_names = nullptr; /**< Pointer to column names */

        private:
            size_t current_end = 0;          /**< Where we are currently in the text buffer */
            size_t current_split_idx = 0;    /**< Where we are currently in the split buffer */
        };

        struct ColumnPositions {
            ColumnPositions() : parent(nullptr) {};
            constexpr ColumnPositions(const RawRowBuffer& _parent,
                size_t _start, unsigned short _size) : parent(&_parent), start(_start), n_cols(_size) {};

            const RawRowBuffer * parent; /**< RawRowBuffer to grab data from */
            size_t start;                /**< Where in split_buffer the array of column positions begins */
            unsigned short n_cols;       /**< Number of columns */

            /// Get the n-th column index
            unsigned short split_at(int n) const;
        };
    }
}
