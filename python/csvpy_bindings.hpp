#pragma once

#include <nanobind/nanobind.h>
#include <nanobind/make_iterator.h>
#include <nanobind/ndarray.h>
#include <nanobind/operators.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>
#include <datetime.h>
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <ctime>
#include <limits>
#include <memory>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "csv.hpp"

namespace nb = nanobind;
using namespace nanobind::literals;
using namespace csv;

void init_python_datetime_api();
nb::object field_to_python(CSVField field, bool cast);
nb::dict read_numpy(
    const std::string& filename,
    nb::object columns = nb::none(),
    bool cast = true,
    nb::object predicate = nb::none()
);
nb::dict data_frame_to_numpy(
    const DataFrame<>& frame,
    const std::vector<std::uint8_t>& deleted_rows,
    nb::object columns = nb::none(),
    bool cast = true,
    nb::object predicate = nb::none()
);

void init_CSVFormat(nb::module_& m);
void init_CSVReader(nb::module_& m);
void init_CSVDocument(nb::module_& m);
void init_CSVPredicate(nb::module_& m);
void init_CSVRow(nb::module_& m);
void init_DataType(nb::module_& m);
void init_CSVField(nb::module_& m);
void init_CSVUtility(nb::module_& m);
