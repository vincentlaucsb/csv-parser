#include "csv.hpp"

#include <chrono>
#include <iostream>
#include <string>
#include <unordered_map>
#include <vector>

namespace {
    using csv::DataFrame;
    using csv::DataFrameExecutor;
    using csv::DataType;

    std::string printable_char(char ch) {
        switch (ch) {
        case '\t': return "\\t";
        case '\n': return "\\n";
        case '\r': return "\\r";
        default: return std::string(1, ch);
        }
    }

    std::string quote_ident(const std::string& ident) {
        std::string out;
        out.reserve(ident.size() + 2);
        out.push_back('"');
        for (char ch : ident) {
            if (ch == '"') {
                out += "\"\"";
            } else {
                out.push_back(ch);
            }
        }
        out.push_back('"');
        return out;
    }

    std::string sql_type_for(DataType type) {
        switch (type) {
        case DataType::CSV_INT8:
        case DataType::CSV_INT16:
            return "SMALLINT";
        case DataType::CSV_INT32:
            return "INTEGER";
        case DataType::CSV_INT64:
            return "BIGINT";
        case DataType::CSV_DOUBLE:
            return "DOUBLE PRECISION";
        case DataType::CSV_NULL:
            return "TEXT";
        case DataType::CSV_STRING:
        default:
            return "TEXT";
        }
    }

    struct ColumnSchemaInfo {
        std::unordered_map<DataType, size_t> type_counts;
        bool nullable = false;
    };

    DataType dominant_type(const std::unordered_map<DataType, size_t>& counts) {
        if (counts.find(DataType::CSV_STRING) != counts.end())
            return DataType::CSV_STRING;
        if (counts.find(DataType::CSV_INT64) != counts.end())
            return DataType::CSV_INT64;
        if (counts.find(DataType::CSV_INT32) != counts.end())
            return DataType::CSV_INT32;
        if (counts.find(DataType::CSV_INT16) != counts.end())
            return DataType::CSV_INT16;
        if (counts.find(DataType::CSV_INT8) != counts.end())
            return DataType::CSV_INT8;
        if (counts.find(DataType::CSV_DOUBLE) != counts.end())
            return DataType::CSV_DOUBLE;
        return DataType::CSV_NULL;
    }
}

int main(int argc, char** argv) {
    using namespace csv;

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " [file]" << std::endl;
        return 1;
    }

    const std::string filename = argv[1];

    try {
        const auto started_at = std::chrono::steady_clock::now();

        CSVReader reader(filename, CSVFormat::guess_csv());
        const std::vector<std::string> col_names = reader.get_col_names();
        const CSVFormat inferred_format = reader.get_format();

        std::vector<ColumnSchemaInfo> schema(col_names.size());

        chunk_parallel_apply(reader, schema,
            [](DataFrame<>::column_type column, ColumnSchemaInfo& info) {
                for (const auto& cell : column) {
                    const DataType type = cell.type();
                    info.type_counts[type]++;
                    info.nullable = info.nullable || (type == DataType::CSV_NULL);
                }
            });

        const auto finished_at = std::chrono::steady_clock::now();
        const auto elapsed_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            finished_at - started_at).count();

        std::cout << "File: " << filename << std::endl;
        std::cout << "Elapsed: " << elapsed_ms << " ms" << std::endl;
        std::cout << "Rows: " << reader.n_rows() << std::endl;
        std::cout << "Columns: " << col_names.size() << std::endl;
        std::cout << std::endl;

        std::cout << "Dialect" << std::endl;
        std::cout << "  Delimiter: " << printable_char(inferred_format.get_delim()) << std::endl;
        std::cout << "  Quote: "
            << (inferred_format.is_quoting_enabled()
                ? printable_char(inferred_format.get_quote_char())
                : std::string("<disabled>"))
            << std::endl;
        std::cout << "  Header row: " << inferred_format.get_header() << std::endl;
        std::cout << std::endl;

        std::cout << "Schema" << std::endl;
        std::cout << "CREATE TABLE inferred_table (" << std::endl;
        for (size_t i = 0; i < col_names.size(); ++i) {
            const auto inferred_type = dominant_type(schema[i].type_counts);
            std::cout << "  " << quote_ident(col_names[i]) << " "
                << sql_type_for(inferred_type)
                << (schema[i].nullable ? " NULL" : " NOT NULL");
            if (i + 1 < col_names.size()) {
                std::cout << ",";
            }
            std::cout << std::endl;
        }
        std::cout << ");" << std::endl;
    }
    catch (const std::exception& ex) {
        std::cerr << "Error: " << ex.what() << std::endl;
        return 1;
    }

    return 0;
}
