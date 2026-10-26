#include "csv_utility.hpp"
#include "data_frame.hpp"

namespace csv {
#if CSV_EXCEPTIONS_ENABLED
    std::unordered_map<std::string, DataType> csv_data_types(CSVReader& reader) {
        CSVError error;
        auto result = try_csv_data_types(reader, error);
        internals::throw_dataframe_error(error);
        return result;
    }
#endif

    std::unordered_map<std::string, DataType> try_csv_data_types(CSVReader& reader, CSVError& error) {
        error.clear();
        std::unordered_map<std::string, DataType> csv_dtypes;
        const auto col_names = reader.get_col_names();
        std::vector<std::unordered_map<DataType, size_t>> type_counts(col_names.size());
        constexpr size_t TYPE_CHUNK_SIZE = 5000;

        // A task owns one column's count map and error slot. Collect errors only
        // after the executor joins, rather than racing on the caller's error.
        std::vector<CSVError> column_errors(col_names.size());
        auto count_column = [&column_errors](DataFrame<>::column_type column,
            std::unordered_map<DataType, size_t>& counts) {
            CSVError& column_error = column_errors[column.index()];
            for (size_t row_index = 0; row_index < column.size(); ++row_index) {
                const auto value = column.try_get_sv(row_index, column_error);
                if (column_error) return;
                counts[internals::data_type(value)]++;
            }
        };
        DataFrameExecutor executor;
        if (!internals::try_for_each_csv_chunk(reader,
            [&executor, &type_counts, &column_errors, &count_column](DataFrame<>& batch, CSVError& failure) {
                if (!batch.try_column_parallel_apply(executor, type_counts, count_column, failure)) return false;
                for (const auto& column_error : column_errors) {
                    if (column_error) {
                        failure = column_error;
                        return false;
                    }
                }
                return true;
            }, error, TYPE_CHUNK_SIZE)) return {};

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
            else if (col[DataType::CSV_BOOL])
                csv_dtypes[col_name] = DataType::CSV_BOOL;
            else if (col[DataType::CSV_TIMESTAMP])
                csv_dtypes[col_name] = DataType::CSV_TIMESTAMP;
            else if (col[DataType::CSV_NULL])
                csv_dtypes[col_name] = DataType::CSV_NULL;
            else
                csv_dtypes[col_name] = DataType::CSV_DOUBLE;
        }

        return csv_dtypes;
    }
}
