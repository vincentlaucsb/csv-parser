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
        switch (error.code) {
        case CSVErrorCode::None:
            return;
        case CSVErrorCode::IndexOutOfBounds:
            if (error.context == CSV_ERROR_INDEX_OUT_OF_BOUNDS) break;
            throw std::out_of_range(std::string(error.context));
        case CSVErrorCode::ColumnNotFound:
        case CSVErrorCode::KeyNotFound:
            throw std::out_of_range(std::string(error.context));
        default:
            break;
        }
        throw_error(error);
    }
}}
#endif
