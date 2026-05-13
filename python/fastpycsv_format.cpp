#include "fastpycsv_bindings.hpp"

void init_CSVFormat(nb::module_& m){
    nb::class_<CSVFormat>(m, "Format")
    .def(nb::init<>())
    .def("delimiter",
             static_cast<CSVFormat& (CSVFormat::*)(const std::vector<char>&)>(&CSVFormat::delimiter),
             "Sets a list of potential delimiters.",
             nb::arg("delim"))
    .def("delimiter",
             static_cast<CSVFormat& (CSVFormat::*)(char)>(&CSVFormat::delimiter),
             "Sets the delimiter of the CSV file.",
             nb::arg("delim"))

    .def("trim", 
        &CSVFormat::trim, 
        "Sets the whitespace characters to be trimmed",
        nb::arg("ws"))

    .def("quote", 
        static_cast<CSVFormat& (CSVFormat::*)(char)>(&CSVFormat::quote),
        "Sets the quote character",
        nb::arg("quote"))

    .def("quote", 
        static_cast<CSVFormat& (CSVFormat::*)(bool)>(&CSVFormat::quote),
        "Turn quoting on or off",
        nb::arg("use_quote"))

    .def("column_names", 
        &CSVFormat::column_names,
        "Sets the column names.",
        nb::arg("names"))

    .def("header_row", 
        &CSVFormat::header_row,
        "Sets the header row",
        nb::arg("row"))
    .def("no_header", 
        &CSVFormat::no_header,
        "Tells the parser that this CSV has no header row")
    .def("variable_columns",
        static_cast<CSVFormat& (CSVFormat::*)(VariableColumnPolicy)>(&CSVFormat::variable_columns),
        "Tells the parser how to handle rows with different column counts",
        nb::arg("policy"))
    .def("eager_field_classification",
        &CSVFormat::eager_field_classification,
        "Precompute scalar field classifications during parsing",
        nb::arg("enabled") = true)
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

