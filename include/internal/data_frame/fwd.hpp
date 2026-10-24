#pragma once
#include "../csv_exceptions.hpp"

namespace csv {
    template<typename KeyType>
    class DataFrame;
    template<typename KeyType>
    class DataFrameColumn;
    template<typename KeyType>
    class DataFrameRow;
}

#if CSV_EXCEPTIONS_ENABLED
namespace csv { namespace internals {
    inline void throw_dataframe_error(const CSVError& error) {
        if (!error) return;
        if ((error.code == CSVErrorCode::IndexOutOfBounds
                && error.context != CSV_ERROR_INDEX_OUT_OF_BOUNDS)
            || error.code == CSVErrorCode::ColumnNotFound
            || error.code == CSVErrorCode::KeyNotFound)
            throw std::out_of_range(error.context);
        throw_error(error);
    }
}}
#endif
