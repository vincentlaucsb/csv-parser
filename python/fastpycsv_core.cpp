#include "fastpycsv_bindings.hpp"

void init_python_datetime_api() {
    PyDateTime_IMPORT;
}

nb::object field_to_python(CSVField field, bool cast) {
    auto field_string = [&field]() {
        const std::string value = field.get<std::string>();
        return nb::str(value.data(), value.size());
    };

    if (!cast) {
        return field_string();
    }

    const DataType field_type = field.type();

    switch (field_type) {
        case DataType::UNKNOWN:
        case DataType::CSV_STRING:
            return field_string();
        case DataType::CSV_NULL:
            return nb::none();
        case DataType::CSV_BOOL:
            return nb::bool_(field.get<bool>());
        case DataType::CSV_INT8:
        case DataType::CSV_INT16:
        case DataType::CSV_INT32:
        case DataType::CSV_INT64:
            return nb::int_(field.get<int64_t>());
        case DataType::CSV_DOUBLE:
            return nb::float_(field.get<double>());
        case DataType::CSV_TIMESTAMP: {
            std::uint64_t milliseconds = 0;
            if (!field.try_parse_timestamp(milliseconds)) {
                return field_string();
            }

            const std::time_t seconds = static_cast<std::time_t>(milliseconds / 1000);
            std::tm utc {};
#if defined(_WIN32)
            gmtime_s(&utc, &seconds);
#else
            gmtime_r(&seconds, &utc);
#endif
            PyObject* datetime = PyDateTime_FromDateAndTime(
                utc.tm_year + 1900,
                utc.tm_mon + 1,
                utc.tm_mday,
                utc.tm_hour,
                utc.tm_min,
                utc.tm_sec,
                static_cast<int>((milliseconds % 1000) * 1000)
            );
            if (datetime == nullptr) {
                throw nb::python_error();
            }
            return nb::steal<nb::object>(datetime);
        }
        default:
            return field_string();
    }

    return field_string();
}

void init_CSVRow(nb::module_& m){
    nb::class_<CSVRow>(m, "Row")
    .def(nb::init<>())
    .def("empty", 
    &CSVRow::empty, 
    "Indicates whether row is empty or not")
    .def("size", 
    &CSVRow::size,
    "Return the number of fields in this row")
    
    .def("get_col_names", 
    &CSVRow::get_col_names,
    "Retrieve this row's associated column names")

    .def("to_json", &CSVRow::to_json, "subset"_a=std::vector<std::string>{})

    .def("to_json_array", &CSVRow::to_json_array, "subset"_a=std::vector<std::string>{})

    .def("__getitem__", [](const CSVRow& row, size_t idx){
        if(idx >= row.size()){
            throw nb::index_error("index out of range");
        }
        return row[idx];
    }, nb::is_operator())

    .def("__getitem__", [](const CSVRow& row, std::string col_name){
        auto column_names = row.get_col_names();
        auto it = std::find(column_names.begin(), column_names.end(), col_name);
        if (it != column_names.end()){
            return row[it - column_names.begin()];
        }else{
            throw nb::index_error(("Can't find a column named " + col_name).c_str());
        }
    }, nb::is_operator());
}

void init_DataType(nb::module_& m){
    nb::enum_<VariableColumnPolicy>(m,
    "VariableColumnPolicy",
    "How to handle rows that are shorter or longer than expected")
    .value("THROW", VariableColumnPolicy::THROW)
    .value("IGNORE_ROW", VariableColumnPolicy::IGNORE_ROW)
    .value("KEEP", VariableColumnPolicy::KEEP)
    .value("KEEP_NON_EMPTY", VariableColumnPolicy::KEEP_NON_EMPTY);

    nb::enum_<DataType>(m,
    "DataType", 
    "Enumerates the different CSV field types that are recognized by this library")
    .value("UNKNOWN" ,DataType::UNKNOWN)
    .value("CSV_NULL", DataType::CSV_NULL, "Empty string")
    .value("CSV_STRING", DataType::CSV_STRING, "Non-numeric string")
    .value("CSV_BOOL", DataType::CSV_BOOL, "Boolean value")
    .value("CSV_INT8", DataType::CSV_INT8, "8-bit integer")
    .value("CSV_INT16", DataType::CSV_INT16, "16-bit integer")
    .value("CSV_INT32", DataType::CSV_INT32, "32-bit integer")
    .value("CSV_INT64", DataType::CSV_INT64, "64-bit integer")
    .value("CSV_BIGINT", DataType::CSV_BIGINT, "Integer too large to fit in 64 bits")
    .value("CSV_DOUBLE", DataType::CSV_DOUBLE, "Floating point value")
    .value("CSV_TIMESTAMP", DataType::CSV_TIMESTAMP, "Timestamp value");
}

void init_CSVField(nb::module_& m){
    nb::class_<CSVField>(m, "Field")
    .def(nb::init<csv::string_view>())
    .def("is_null", 
    &CSVField::is_null, 
    "Returns true if field is an empty string or string of whitespace characters")
    .def("get_sv", 
    &CSVField::get_sv,
    "Return a string view over the field's contents")
    .def("is_str", 
    &CSVField::is_str,
    "Returns true if field is a non-numeric, non-empty string")
    .def("is_num", 
    &CSVField::is_num,
    "Returns true if field is an integer or float")
    .def("is_int", 
    &CSVField::is_int,
    "Returns true if field is an integer")
    .def("is_float", 
    &CSVField::is_float,
    "Returns true if field is a floating point value")
    .def("is_bool",
    &CSVField::is_bool,
    "Returns true if field is a boolean value")
    .def("is_timestamp",
    &CSVField::is_timestamp,
    "Returns true if field is a timestamp value")
    .def("type",
    &CSVField::type,
    "Return the type of the underlying CSV data")
    .def("get_int", &CSVField::get<int64_t>)
    .def("get_bool", &CSVField::get<bool>)
    .def("get_str", &CSVField::get<std::string>)
    .def("get_double", &CSVField::get<double>)
    .def("get_float", &CSVField::get<float>);
}

void init_CSVUtility(nb::module_& m){
    nb::class_<CSVFileInfo>(m, "CSVFileInfo")
    .def_ro("filename",&CSVFileInfo::filename)
    .def_ro("col_names", &CSVFileInfo::col_names)
    .def_ro("delim", &CSVFileInfo::delim)
    .def_ro("n_rows", &CSVFileInfo::n_rows)
    .def_ro("n_cols", &CSVFileInfo::n_cols);
    
    m.def("parse", 
    &parse,
    "Shorthand function for parsing an in-memory CSV string",
    nb::arg("in"), nb::arg("format"))
    .def("parse_no_header",
    &parse_no_header,
    "Parses a CSV string with no headers",
    nb::arg("in"))
    .def("get_col_pos",
    &get_col_pos,
    "Find the position of a column in a CSV file or CSV_NOT_FOUND otherwise",
    nb::arg("filename"),
    nb::arg("col_name"),
    nb::arg("format"))
    .def("get_file_info",
    &get_file_info,
    "Get basic information about a CSV file",
    nb::arg("filename"))
    .def("csv_data_types",
    [](csv::string_view filename) {
        return csv_data_types(filename);
    },
    "Return a data type for each column such that every value in a column can be converted to the corresponding data type without data loss.",
    nb::arg("filename"))
    .def("csv_data_types",
    [](csv::string_view filename, CSVFormat format) {
        return csv_data_types(filename, format);
    },
    "Return a data type for each column such that every value in a column can be converted to the corresponding data type without data loss.",
    nb::arg("filename"),
    nb::arg("format"))
    .def("read_numpy",
    [](const std::string& filename, nb::object columns, bool cast, nb::object predicate) {
        return read_numpy(filename, columns, cast, predicate, CSVFormat::guess_csv());
    },
    "Parse a CSV file into a dict of NumPy arrays keyed by column name.",
    nb::arg("path"),
    nb::arg("columns") = nb::none(),
    nb::arg("cast") = true,
    nb::arg("predicate") = nb::none());
}
