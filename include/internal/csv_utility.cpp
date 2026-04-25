#include "csv_utility.hpp"
#include "data_frame.hpp"

namespace csv {
    CSV_INLINE std::unordered_map<std::string, DataType> csv_data_types(CSVReader& reader) {
        std::unordered_map<std::string, DataType> csv_dtypes;
        const auto col_names = reader.get_col_names();
        std::vector<std::unordered_map<DataType, size_t>> type_counts(col_names.size());
        constexpr size_t TYPE_CHUNK_SIZE = 5000;

        chunk_parallel_apply(reader, type_counts,
            [](DataFrame<>::column_type column, std::unordered_map<DataType, size_t>& counts) {
                for (size_t row_index = 0; row_index < column.size(); ++row_index) {
                    counts[column[row_index].type()]++;
                }
            },
            TYPE_CHUNK_SIZE
        );

        for (size_t i = 0; i < col_names.size(); i++) {
            auto& col = type_counts[i];
            auto& col_name = col_names[i];

            if (col[DataType::CSV_STRING])
                csv_dtypes[col_name] = DataType::CSV_STRING;
            else if (col[DataType::CSV_INT64])
                csv_dtypes[col_name] = DataType::CSV_INT64;
            else if (col[DataType::CSV_INT32])
                csv_dtypes[col_name] = DataType::CSV_INT32;
            else if (col[DataType::CSV_INT16])
                csv_dtypes[col_name] = DataType::CSV_INT16;
            else if (col[DataType::CSV_INT8])
                csv_dtypes[col_name] = DataType::CSV_INT8;
            else
                csv_dtypes[col_name] = DataType::CSV_DOUBLE;
        }

        return csv_dtypes;
    }
}
