#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/operators.h>
#include <utility>
#include <vector>
#include <algorithm>
#include "csv.hpp"
namespace py = pybind11;
using namespace pybind11::literals;
using namespace csv;

void init_CSVFormat(py::module& m){
    py::class_<CSVFormat>(m, "Format")
    .def(py::init<>())
    .def("delimiter",
             py::overload_cast<const std::vector<char>&>(&CSVFormat::delimiter),
             "Sets a list of potential delimiters.",
             py::arg("delim"))
    .def("delimiter",
             py::overload_cast<char>(&CSVFormat::delimiter),
             "Sets the delimiter of the CSV file.",
             py::arg("delim"))

    .def("trim", 
        &CSVFormat::trim, 
        "Sets the whitespace characters to be trimmed",
        py::arg("ws"))

    .def("quote", 
        py::overload_cast<char>(&CSVFormat::quote),
        "Sets the quote character",
        py::arg("quote"))

    .def("quote", 
        py::overload_cast<bool>(&CSVFormat::quote),
        "Turn quoting on or off",
        py::arg("use_quote"))

    .def("column_names", 
        &CSVFormat::column_names,
        "Sets the column names.",
        py::arg("names"))

    .def("header_row", 
        &CSVFormat::header_row,
        "Sets the header row",
        py::arg("row"))
    .def("no_header", 
        &CSVFormat::no_header,
        "Tells the parser that this CSV has no header row")
    .def("is_quoting_enabled",
    &CSVFormat::is_quoting_enabled)
    .def("get_quote_char",
    &CSVFormat::get_quote_char)
    .def("get_header", &CSVFormat::get_header)
    .def("get_possible_delims",
    &CSVFormat::get_possible_delims)
    .def("get_trim_chars",
    &CSVFormat::get_trim_chars);
}

void init_CSVReader(py::module& m){
    py::class_<CSVReader>(m, "Reader")
    .def(py::init<csv::string_view, CSVFormat>(), 
    "filename"_a, 
    "format"_a=CSVFormat::guess_csv())
    .def("eof", 
    &CSVReader::eof,
    "Returns true if we have reached end of file")
    .def("get_format", 
    &CSVReader::get_format)
    .def("empty", 
    &CSVReader::empty)
    .def("n_rows", 
    &CSVReader::n_rows,
    "Retrieves the number of rows that have been read so far")
    .def("utf8_bom", 
    &CSVReader::utf8_bom,
    "Whether or not CSV was prefixed with a UTF-8 bom")
    .def("__iter__", 
    [](CSVReader& reader){return py::make_iterator(reader.begin(), reader.end());},
    py::keep_alive<0, 1>());
}

void init_CSVRow(py::module& m){
    py::class_<CSVRow>(m, "Row")
    .def(py::init<>())
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
            throw py::index_error("index out of range");
        }
        return row[idx];
    }, py::is_operator())

    .def("__getitem__", [](const CSVRow& row, std::string col_name){
        auto column_names = row.get_col_names();
        auto it = std::find(column_names.begin(), column_names.end(), col_name);
        if (it != column_names.end()){
            return row[it - column_names.begin()];
        }else{
            throw py::index_error("Can't find a column named " + col_name);
        }
    }, py::is_operator());
}

void init_DataType(py::module& m){
    py::enum_<DataType>(m,
    "DataType", 
    py::arithmetic(),
    "Enumerates the different CSV field types that are recognized by this library")
    .value("UNKNOWN" ,DataType::UNKNOWN)
    .value("CSV_NULL", DataType::CSV_NULL, "Empty string")
    .value("CSV_STRING", DataType::CSV_STRING, "Non-numeric string")
    .value("CSV_INT8", DataType::CSV_INT8, "8-bit integer")
    .value("CSV_INT16", DataType::CSV_INT16, "16-bit integer")
    .value("CSV_INT32", DataType::CSV_INT32, "32-bit integer")
    .value("CSV_INT64", DataType::CSV_INT64, "64-bit integer")
    .value("CSV_DOUBLE", DataType::CSV_DOUBLE, "Floating point value");
}

void init_CSVField(py::module& m){
    py::class_<CSVField>(m, "Field")
    .def(py::init<csv::string_view>())
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
    .def("type",
    &CSVField::type,
    "Return the type of the underlying CSV data")
    .def("get_int", &CSVField::get<int64_t>)
    .def("get_str", &CSVField::get<std::string>)
    .def("get_double", &CSVField::get<double>)
    .def("get_float", &CSVField::get<float>);
}

void init_CSVUtility(py::module& m){
    py::class_<CSVFileInfo>(m, "CSVFileInfo")
    .def_readonly("filename",&CSVFileInfo::filename)
    .def_readonly("col_names", &CSVFileInfo::col_names)
    .def_readonly("delim", &CSVFileInfo::delim)
    .def_readonly("n_rows", &CSVFileInfo::n_rows)
    .def_readonly("n_cols", &CSVFileInfo::n_cols);
    
    m.def("parse", 
    &parse,
    "Shorthand function for parsing an in-memory CSV string",
    py::arg("in"), py::arg("format"))
    .def("parse_no_header",
    &parse_no_header,
    "Parses a CSV string with no headers",
    py::arg("in"))
    .def("get_col_pos",
    &get_col_pos,
    "Find the position of a column in a CSV file or CSV_NOT_FOUND otherwise",
    py::arg("filename"),
    py::arg("col_name"),
    py::arg("format"))
    .def("get_file_info",
    &get_file_info,
    "Get basic information about a CSV file",
    py::arg("filename"))
    .def("csv_data_types",
    &csv_data_types,
    "Return a data type for each column such that every value in a column can be converted to the corresponding data type without data loss.",
    py::arg("filename"));
}

void init_CSVStat(py::module& m){
    py::class_<CSVStat>(m, "CSVStat")
    .def(py::init<csv::string_view, CSVFormat>(),
    "filename"_a,
    "format"_a=CSVFormat::guess_csv())
    .def("get_mean",
    &CSVStat::get_mean,
    "Return current means")
    .def("get_variance",
    &CSVStat::get_variance,
    "Return current variances")
    .def("get_mins",
    &CSVStat::get_mins,
    "Return current mins")
    .def("get_maxes",
    &CSVStat::get_maxes,
    "Return current maxes")
    .def("get_counts",
    &CSVStat::get_counts,
    "Get counts for each column")
    .def("get_dtypes",
    &CSVStat::get_dtypes,
    "Get data type counts for each column")
    .def("get_col_names",
    &CSVStat::get_col_names,
    "Return the CSV's column names as a List of strings.");
}

PYBIND11_MODULE(csvpy, m){
    m.doc() = "A modern C++ library for reading, writing, and analyzing CSV (and similar) files.";
    init_CSVFormat(m);
    init_CSVReader(m);
    init_CSVRow(m);
    init_DataType(m);
    init_CSVField(m);
    init_CSVUtility(m);
    init_CSVStat(m);
}