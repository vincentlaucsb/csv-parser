#include "csvpy_bindings.hpp"

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

    nb::dict as_dict(nb::object selected = nb::none()) const {
        const std::vector<std::string>& columns = this->columns_or_throw();
        nb::dict out;

        if (selected.is_none()) {
            const size_t n = (std::min)(columns.size(), this->row_.size());
            for (size_t i = 0; i < n; ++i) {
                out[nb::str(columns[i].data(), columns[i].size())] = this->field_to_python(this->row_[i]);
            }
            return out;
        }

        const std::vector<std::string> selected_columns = nb::cast<std::vector<std::string>>(selected);
        for (const auto& column : selected_columns) {
            out[nb::str(column.data(), column.size())] = this->field_to_python(this->field_at(this->index_of(column)));
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

    const std::vector<std::string>& get_col_names() const {
        return this->reader_.get_col_names();
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
    .def("as_dict", &LazyCSVRow::as_dict, "columns"_a = nb::none())
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
    .def_prop_ro("fieldnames", &LazyCSVRowReader::get_col_names)
    .def("__next__", &LazyCSVRowReader::next);
}
