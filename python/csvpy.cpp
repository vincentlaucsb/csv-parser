#include <nanobind/nanobind.h>
#include <nanobind/make_iterator.h>
#include <nanobind/ndarray.h>
#include <nanobind/operators.h>
#include <nanobind/stl/chrono.h>
#include <nanobind/stl/string.h>
#include <nanobind/stl/string_view.h>
#include <nanobind/stl/vector.h>
#include <datetime.h>
#include <chrono>
#include <algorithm>
#include <ctime>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <utility>
#include "csv.hpp"
namespace nb = nanobind;
using namespace nanobind::literals;
using namespace csv;

enum class NumpyColumnKind {
    UNKNOWN,
    INT64,
    FLOAT64,
    BOOL,
    STRING
};

struct NumpyColumnPlan {
    std::string name;
    size_t index = 0;
    NumpyColumnKind kind = NumpyColumnKind::UNKNOWN;
    bool nullable = false;
};

inline bool is_integral_type(DataType type) noexcept {
    return type >= DataType::CSV_INT8 && type <= DataType::CSV_INT64;
}

inline void merge_numpy_column_type(NumpyColumnPlan& column, DataType type) {
    if (type == DataType::CSV_NULL) {
        column.nullable = true;
        return;
    }

    NumpyColumnKind observed = NumpyColumnKind::STRING;
    if (is_integral_type(type)) {
        observed = NumpyColumnKind::INT64;
    }
    else if (type == DataType::CSV_DOUBLE) {
        observed = NumpyColumnKind::FLOAT64;
    }
    else if (type == DataType::CSV_BOOL) {
        observed = NumpyColumnKind::BOOL;
    }

    if (column.kind == NumpyColumnKind::UNKNOWN) {
        column.kind = observed;
        return;
    }

    if (column.kind == observed) {
        return;
    }

    if ((column.kind == NumpyColumnKind::INT64 && observed == NumpyColumnKind::FLOAT64)
        || (column.kind == NumpyColumnKind::FLOAT64 && observed == NumpyColumnKind::INT64)) {
        column.kind = NumpyColumnKind::FLOAT64;
        return;
    }

    column.kind = NumpyColumnKind::STRING;
}

inline std::vector<NumpyColumnPlan> make_numpy_column_plan(
    const std::vector<std::string>& names,
    nb::object columns
) {
    if (names.empty()) {
        throw std::runtime_error("csvpy.read_numpy requires a header row or selected column names");
    }

    std::vector<NumpyColumnPlan> plan;
    if (columns.is_none()) {
        plan.reserve(names.size());
        for (size_t i = 0; i < names.size(); ++i) {
            NumpyColumnPlan column;
            column.name = names[i];
            column.index = i;
            plan.push_back(column);
        }
        return plan;
    }

    std::vector<std::string> selected = nb::cast<std::vector<std::string>>(columns);
    plan.reserve(selected.size());
    for (const auto& name : selected) {
        const auto it = std::find(names.begin(), names.end(), name);
        if (it == names.end()) {
            throw std::runtime_error("selected column not found: " + name);
        }

        NumpyColumnPlan column;
        column.name = name;
        column.index = static_cast<size_t>(it - names.begin());
        plan.push_back(column);
    }
    return plan;
}

template<typename T>
nb::object vector_to_numpy_array(std::unique_ptr<std::vector<T>> values) {
    const size_t size = values->size();
    T* data = values->data();
    nb::capsule owner(values.release(), [](void* ptr) noexcept {
        delete static_cast<std::vector<T>*>(ptr);
    });
    return nb::cast(nb::ndarray<nb::numpy, T>(data, { size }, owner));
}

nb::object string_vector_to_numpy_array(const std::vector<std::string>& values) {
    nb::module_ np = nb::module_::import_("numpy");
    nb::object string_dtype;
    try {
        string_dtype = np.attr("dtypes").attr("StringDType")();
    }
    catch (const nb::python_error&) {
        throw std::runtime_error("csvpy.read_numpy string columns require NumPy 2.x with np.dtypes.StringDType");
    }

    PyObject* raw_list = PyList_New(static_cast<Py_ssize_t>(values.size()));
    if (raw_list == nullptr) {
        throw nb::python_error();
    }

    nb::list list = nb::steal<nb::list>(raw_list);
    for (size_t i = 0; i < values.size(); ++i) {
        nb::str value(values[i].data(), values[i].size());
        PyList_SET_ITEM(list.ptr(), static_cast<Py_ssize_t>(i), value.release().ptr());
    }

    return np.attr("array")(list, "dtype"_a = string_dtype);
}

nb::object bool_vector_to_numpy_array(std::unique_ptr<std::vector<std::uint8_t>> values) {
    nb::module_ np = nb::module_::import_("numpy");
    nb::object raw = vector_to_numpy_array<std::uint8_t>(std::move(values));
    return raw.attr("astype")(np.attr("bool_"), "copy"_a = false);
}

struct NumpyColumnBuffer {
    NumpyColumnPlan plan;
    std::unique_ptr<std::vector<std::int64_t>> ints;
    std::unique_ptr<std::vector<double>> floats;
    std::unique_ptr<std::vector<std::uint8_t>> bools;
    std::vector<std::string> strings;

    explicit NumpyColumnBuffer(NumpyColumnPlan column)
        : plan(std::move(column)) {}

    void reserve(size_t rows) {
        switch (this->storage_kind()) {
        case NumpyColumnKind::INT64:
            this->ints.reset(new std::vector<std::int64_t>());
            this->ints->reserve(rows);
            break;
        case NumpyColumnKind::BOOL:
            this->bools.reset(new std::vector<std::uint8_t>());
            this->bools->reserve(rows);
            break;
        case NumpyColumnKind::FLOAT64:
        case NumpyColumnKind::UNKNOWN:
            this->floats.reset(new std::vector<double>());
            this->floats->reserve(rows);
            break;
        case NumpyColumnKind::STRING:
            this->strings.reserve(rows);
            break;
        }
    }

    NumpyColumnKind storage_kind() const noexcept {
        if (this->plan.kind == NumpyColumnKind::INT64 && !this->plan.nullable) {
            return NumpyColumnKind::INT64;
        }
        if (this->plan.kind == NumpyColumnKind::BOOL && !this->plan.nullable) {
            return NumpyColumnKind::BOOL;
        }
        if (this->plan.kind == NumpyColumnKind::STRING) {
            return NumpyColumnKind::STRING;
        }
        return NumpyColumnKind::FLOAT64;
    }

    void append(CSVField field) {
        switch (this->storage_kind()) {
        case NumpyColumnKind::INT64:
            this->ints->push_back(field.get<std::int64_t>());
            break;
        case NumpyColumnKind::BOOL:
            this->bools->push_back(field.get<bool>() ? 1 : 0);
            break;
        case NumpyColumnKind::FLOAT64:
        case NumpyColumnKind::UNKNOWN:
            if (field.is_null()) {
                this->floats->push_back((std::numeric_limits<double>::quiet_NaN)());
            }
            else if (this->plan.kind == NumpyColumnKind::BOOL) {
                this->floats->push_back(field.get<bool>() ? 1.0 : 0.0);
            }
            else {
                this->floats->push_back(field.get<double>());
            }
            break;
        case NumpyColumnKind::STRING:
            this->strings.push_back(field.get<std::string>());
            break;
        }
    }

    nb::object into_python() {
        switch (this->storage_kind()) {
        case NumpyColumnKind::INT64:
            return vector_to_numpy_array<std::int64_t>(std::move(this->ints));
        case NumpyColumnKind::BOOL:
            return bool_vector_to_numpy_array(std::move(this->bools));
        case NumpyColumnKind::FLOAT64:
        case NumpyColumnKind::UNKNOWN:
            return vector_to_numpy_array<double>(std::move(this->floats));
        case NumpyColumnKind::STRING:
            return string_vector_to_numpy_array(this->strings);
        }

        throw std::runtime_error("unreachable read_numpy column kind");
    }
};

nb::dict read_numpy(const std::string& filename, nb::object columns = nb::none(), bool cast = true) {
    CSVFormat format = CSVFormat::guess_csv();
    if (cast) {
        format.eager_field_classification();
    }

    CSVReader inference_reader(filename, format);
    std::vector<NumpyColumnPlan> plan = make_numpy_column_plan(inference_reader.get_col_names(), columns);
    size_t rows = 0;
    CSVRow row;
    while (inference_reader.read_row(row)) {
        ++rows;
        for (auto& column : plan) {
            if (column.index >= row.size()) {
                column.nullable = true;
                continue;
            }

            if (!cast) {
                column.kind = NumpyColumnKind::STRING;
                continue;
            }

            merge_numpy_column_type(column, row[column.index].type());
        }
    }

    for (auto& column : plan) {
        if (column.kind == NumpyColumnKind::UNKNOWN) {
            column.kind = cast ? NumpyColumnKind::FLOAT64 : NumpyColumnKind::STRING;
        }
    }

    std::vector<NumpyColumnBuffer> buffers;
    buffers.reserve(plan.size());
    for (auto& column : plan) {
        buffers.emplace_back(std::move(column));
        buffers.back().reserve(rows);
    }

    CSVReader materialize_reader(filename, format);
    while (materialize_reader.read_row(row)) {
        for (auto& buffer : buffers) {
            if (buffer.plan.index >= row.size()) {
                if (buffer.storage_kind() == NumpyColumnKind::STRING) {
                    buffer.strings.push_back(std::string());
                }
                else {
                    buffer.floats->push_back((std::numeric_limits<double>::quiet_NaN)());
                }
                continue;
            }

            buffer.append(row[buffer.plan.index]);
        }
    }

    nb::dict out;
    for (auto& buffer : buffers) {
        out[nb::str(buffer.plan.name.data(), buffer.plan.name.size())] = buffer.into_python();
    }
    return out;
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

class LazyCSVRow {
public:
    LazyCSVRow(CSVRow row, bool cast)
        : row_(std::move(row)),
          cast_(cast) {}

    size_t size() const noexcept {
        return this->row_.size();
    }

    nb::object get_item(nb::handle key) const {
        if (PySlice_Check(key.ptr())) {
            return this->slice(key);
        }

        if (PyIndex_Check(key.ptr())) {
            return this->field_to_python(this->field_at(this->normalize_index(PyNumber_AsSsize_t(key.ptr(), PyExc_IndexError))));
        }

        if (PyUnicode_Check(key.ptr())) {
            return this->field_to_python(this->field_at(this->index_of(nb::cast<std::string>(key))));
        }

        throw nb::type_error("row indices must be integers, slices, or column names");
    }

    nb::iterator iter() const {
        return nb::make_iterator(nb::type<LazyCSVRow>(), "iterator", FieldIterator(this, 0), FieldIterator(this, this->row_.size()));
    }

    nb::list as_list() const {
        PyObject* raw_out = PyList_New(static_cast<Py_ssize_t>(this->row_.size()));
        if (raw_out == nullptr) {
            throw nb::python_error();
        }

        nb::list out = nb::steal<nb::list>(raw_out);
        for (size_t i = 0; i < this->row_.size(); ++i) {
            nb::object value = this->field_to_python(this->row_[i]);
            PyList_SET_ITEM(out.ptr(), static_cast<Py_ssize_t>(i), value.release().ptr());
        }
        return out;
    }

    nb::dict as_dict() const {
        const std::vector<std::string>& columns = this->columns_or_throw();
        nb::dict out;
        const size_t n = (std::min)(columns.size(), this->row_.size());
        for (size_t i = 0; i < n; ++i) {
            out[nb::str(columns[i].data(), columns[i].size())] = this->field_to_python(this->row_[i]);
        }
        return out;
    }

    const std::vector<std::string>& get_col_names() const {
        return this->columns_or_throw();
    }

    std::string get_str(nb::handle key) const {
        return this->field_at(this->index_from_key(key)).get<std::string>();
    }

    std::int64_t get_int(nb::handle key) const {
        return this->field_at(this->index_from_key(key)).get<std::int64_t>();
    }

    double get_float(nb::handle key) const {
        return this->field_at(this->index_from_key(key)).get<double>();
    }

    bool get_bool(nb::handle key) const {
        return this->field_at(this->index_from_key(key)).get<bool>();
    }

    DataType type(nb::handle key) const {
        return this->field_at(this->index_from_key(key)).type();
    }

private:
    class FieldIterator {
    public:
        FieldIterator(const LazyCSVRow* row, size_t index)
            : row_(row),
              index_(index) {}

        nb::object operator*() const {
            return this->row_->field_to_python(this->row_->row_[this->index_]);
        }

        FieldIterator& operator++() {
            ++this->index_;
            return *this;
        }

        bool operator!=(const FieldIterator& other) const {
            return this->index_ != other.index_;
        }

        bool operator==(const FieldIterator& other) const {
            return this->index_ == other.index_;
        }

    private:
        const LazyCSVRow* row_;
        size_t index_;
    };

    CSVRow row_;
    bool cast_ = false;

    Py_ssize_t normalize_index(Py_ssize_t index) const {
        const Py_ssize_t row_size = static_cast<Py_ssize_t>(this->row_.size());
        if (index < 0) {
            index += row_size;
        }

        if (index < 0 || index >= row_size) {
            throw nb::index_error("row index out of range");
        }

        return index;
    }

    size_t index_from_key(nb::handle key) const {
        if (PyIndex_Check(key.ptr())) {
            return static_cast<size_t>(this->normalize_index(PyNumber_AsSsize_t(key.ptr(), PyExc_IndexError)));
        }

        if (PyUnicode_Check(key.ptr())) {
            return this->index_of(nb::cast<std::string>(key));
        }

        throw nb::type_error("row accessor expects an integer index or column name");
    }

    size_t index_of(const std::string& col_name) const {
        const std::vector<std::string>& columns = this->columns_or_throw();
        const auto it = std::find(columns.begin(), columns.end(), col_name);
        if (it == columns.end()) {
            throw nb::index_error(("Can't find a column named " + col_name).c_str());
        }

        const size_t index = static_cast<size_t>(it - columns.begin());
        if (index >= this->row_.size()) {
            throw nb::index_error("column index out of range");
        }
        return index;
    }

    const std::vector<std::string>& columns_or_throw() const {
        const auto& columns = this->row_.get_col_names();
        if (columns.empty()) {
            throw nb::value_error("row has no column names");
        }
        return columns;
    }

    CSVField field_at(size_t index) const {
        if (index >= this->row_.size()) {
            throw nb::index_error("row index out of range");
        }
        return this->row_[index];
    }

    nb::object field_to_python(CSVField field) const {
        return ::field_to_python(std::move(field), this->cast_);
    }

    nb::list slice(nb::handle key) const {
        Py_ssize_t start = 0;
        Py_ssize_t stop = 0;
        Py_ssize_t step = 0;
        Py_ssize_t slicelength = 0;
        if (PySlice_GetIndicesEx(key.ptr(), static_cast<Py_ssize_t>(this->row_.size()), &start, &stop, &step, &slicelength) < 0) {
            throw nb::python_error();
        }

        PyObject* raw_out = PyList_New(slicelength);
        if (raw_out == nullptr) {
            throw nb::python_error();
        }

        nb::list out = nb::steal<nb::list>(raw_out);
        Py_ssize_t current = start;
        for (Py_ssize_t i = 0; i < slicelength; ++i) {
            nb::object value = this->field_to_python(this->row_[static_cast<size_t>(current)]);
            PyList_SET_ITEM(out.ptr(), i, value.release().ptr());
            current += step;
        }
        return out;
    }
};

class LazyCSVRowReader {
public:
    LazyCSVRowReader(
        const std::string& filename,
        CSVFormat format,
        bool cast,
        size_t batch_size = 8192
    ) : reader_(filename, cast ? format.eager_field_classification() : format),
        cast_(cast) {
        (void)batch_size;
    }

    LazyCSVRowReader& iter() {
        return *this;
    }

    LazyCSVRow next() {
        CSVRow row;
        if (!this->reader_.read_row(row)) {
            throw nb::stop_iteration();
        }

        return LazyCSVRow(std::move(row), this->cast_);
    }

private:
    CSVReader reader_;
    bool cast_ = false;
};

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

void init_CSVReader(nb::module_& m){
    nb::class_<CSVReader>(m, "Reader")
    .def(nb::init<csv::string_view>(),
    "filename"_a)
    .def(nb::init<csv::string_view, CSVFormat>(),
    "filename"_a,
    "format"_a)
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
    [](CSVReader& reader) {
        return nb::make_iterator(nb::type<CSVReader>(), "iterator", reader.begin(), reader.end());
    },
    nb::keep_alive<0, 1>());

    nb::class_<LazyCSVRow>(m, "_LazyRow")
    .def("__len__", &LazyCSVRow::size)
    .def("__getitem__", &LazyCSVRow::get_item, nb::is_operator())
    .def("__iter__", &LazyCSVRow::iter, nb::keep_alive<0, 1>())
    .def("as_list", &LazyCSVRow::as_list)
    .def("as_dict", &LazyCSVRow::as_dict)
    .def("get_col_names", &LazyCSVRow::get_col_names)
    .def("get_str", &LazyCSVRow::get_str)
    .def("get_int", &LazyCSVRow::get_int)
    .def("get_float", &LazyCSVRow::get_float)
    .def("get_bool", &LazyCSVRow::get_bool)
    .def("type", &LazyCSVRow::type);

    nb::class_<LazyCSVRowReader>(m, "_RowsReader")
    .def(nb::init<const std::string&, CSVFormat, bool, size_t>(),
        "filename"_a,
        "format"_a,
        "cast"_a = false,
        "batch_size"_a = 8192)
    .def("__iter__",
        &LazyCSVRowReader::iter,
        nb::rv_policy::reference_internal)
    .def("__next__", &LazyCSVRowReader::next);
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
    &read_numpy,
    "Parse a CSV file into a dict of NumPy arrays keyed by column name.",
    nb::arg("path"),
    nb::arg("columns") = nb::none(),
    nb::arg("cast") = true);
}

NB_MODULE(csvpy, m){
    PyDateTime_IMPORT;
    m.doc() = "A modern C++ library for reading, writing, and analyzing CSV (and similar) files.";
    init_CSVFormat(m);
    init_CSVReader(m);
    init_CSVRow(m);
    init_DataType(m);
    init_CSVField(m);
    init_CSVUtility(m);
}
