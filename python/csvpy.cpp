#include "csvpy_bindings.hpp"

NB_MODULE(csvpy, m) {
    init_python_datetime_api();
    m.doc() = "A modern C++ library for reading, writing, and analyzing CSV (and similar) files.";
    init_CSVFormat(m);
    init_CSVReader(m);
    init_CSVPredicate(m);
    init_CSVNumpy(m);
    init_CSVWriter(m);
    init_CSVRow(m);
    init_DataType(m);
    init_CSVField(m);
    init_CSVUtility(m);
}
