#include "csvpy_bindings.hpp"
#include "csvpy_predicate.hpp"

class CSVDocumentNative;

class CSVDocumentRow {
public:
    CSVDocumentRow(CSVDocumentNative* document, size_t row_index, size_t generation)
        : document_(document),
          row_index_(row_index),
          generation_(generation) {}

    size_t size() const;
    nb::object get_item(nb::handle key) const;
    nb::iterator iter() const;
    nb::list as_list() const;
    nb::dict as_dict() const;
    const std::vector<std::string>& get_col_names() const;
    bool remove();
    std::string get_str(nb::handle key) const;
    std::int64_t get_int(nb::handle key) const;
    double get_float(nb::handle key) const;
    bool get_bool(nb::handle key) const;
    DataType type(nb::handle key) const;

private:
    class FieldIterator {
    public:
        FieldIterator(const CSVDocumentRow* row, size_t index)
            : row_(row),
              index_(index) {}

        nb::object operator*() const {
            return this->row_->field_to_python(this->index_);
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
        const CSVDocumentRow* row_;
        size_t index_;
    };

    CSVDocumentNative* document_ = nullptr;
    size_t row_index_ = 0;
    size_t generation_ = 0;

    Py_ssize_t normalize_index(Py_ssize_t index) const;
    size_t index_from_key(nb::handle key) const;
    size_t index_of(const std::string& col_name) const;
    DataFrameCell cell_at(size_t col_index) const;
    nb::object field_to_python(size_t col_index) const;
    nb::list slice(nb::handle key) const;
};

class CSVDocumentIterator {
public:
    CSVDocumentIterator(CSVDocumentNative* document, size_t index, size_t generation)
        : document_(document),
          index_(index),
          generation_(generation) {}

    CSVDocumentIterator& iter() {
        return *this;
    }

    CSVDocumentRow next();

private:
    CSVDocumentNative* document_ = nullptr;
    size_t index_ = 0;
    size_t generation_ = 0;
};

class CSVDocumentNative {
public:
    CSVDocumentNative(const std::string& filename, CSVFormat format, bool cast)
        : cast_(cast) {
        CSVReader reader(filename, cast ? format.eager_field_classification() : format);
        this->frame_ = DataFrame<>(reader);
        this->deleted_rows_.assign(this->frame_.size(), 0);
    }

    CSVDocumentNative(DataFrame<> frame, bool cast)
        : frame_(std::move(frame)),
          deleted_rows_(this->frame_.size(), 0),
          cast_(cast) {}

    CSVDocumentIterator iter() {
        this->require_no_pending_deletes();
        return CSVDocumentIterator(this, 0, this->generation_);
    }

    CSVDocumentRow at(Py_ssize_t index) {
        this->require_no_pending_deletes();
        return CSVDocumentRow(this, this->normalize_index(index), this->generation_);
    }

    size_t size() const noexcept {
        return this->frame_.size() - this->pending_delete_count_;
    }

    bool has_pending_deletes() const noexcept {
        return this->pending_delete_count_ != 0;
    }

    size_t materialize_deletes() {
        const size_t removed = this->pending_delete_count_;
        if (removed == 0) {
            return 0;
        }

        for (size_t i = this->deleted_rows_.size(); i > 0; --i) {
            const size_t row_index = i - 1;
            if (this->deleted_rows_[row_index]) {
                this->frame_.at(row_index).erase();
            }
        }

        this->deleted_rows_.assign(this->frame_.size(), 0);
        this->pending_delete_count_ = 0;
        ++this->generation_;
        return removed;
    }

    size_t discard_deletes() {
        const size_t discarded = this->pending_delete_count_;
        if (discarded == 0) {
            return 0;
        }

        std::fill(this->deleted_rows_.begin(), this->deleted_rows_.end(), 0);
        this->pending_delete_count_ = 0;
        ++this->generation_;
        return discarded;
    }

    nb::dict to_numpy(nb::object columns = nb::none(), bool cast = true, nb::object predicate = nb::none()) {
        return data_frame_to_numpy(this->frame_, this->deleted_rows_, columns, cast, predicate);
    }

    size_t delete_where(nb::object predicate) {
        const RowPredicate* row_predicate = optional_row_predicate(predicate);
        if (!row_predicate) {
            throw nb::type_error("delete_where() requires a predicate created by csvpy.equal()");
        }

        return mark_matching_rows(this->frame_, this->deleted_rows_, this->pending_delete_count_, *row_predicate);
    }

    CSVDocumentNative filter(nb::object predicate) const {
        const RowPredicate* row_predicate = optional_row_predicate(predicate);
        if (!row_predicate) {
            throw nb::type_error("filter() requires a predicate created by csvpy.equal()");
        }

        std::vector<std::uint8_t> include_rows = included_rows_for_predicate(this->frame_, this->deleted_rows_, *row_predicate);
        return CSVDocumentNative(this->frame_.selected_rows(include_rows), this->cast_);
    }

    void validate_generation(size_t generation) const {
        if (generation != this->generation_) {
            throw std::runtime_error("CSVDocument row handle was invalidated by materialize_deletes() or discard_deletes()");
        }
    }

    bool mark_delete(size_t row_index, size_t generation) {
        this->validate_generation(generation);
        if (row_index >= this->frame_.size()) {
            throw nb::index_error("row index out of range");
        }

        if (this->deleted_rows_[row_index]) {
            return false;
        }

        this->deleted_rows_[row_index] = 1;
        ++this->pending_delete_count_;
        return true;
    }

    size_t row_size(size_t row_index, size_t generation) {
        this->validate_generation(generation);
        return this->frame_.at(row_index).size();
    }

    const std::vector<std::string>& columns() const noexcept {
        return this->frame_.columns();
    }

    DataFrameCell cell_at(size_t row_index, size_t col_index, size_t generation) {
        this->validate_generation(generation);
        if (col_index >= this->frame_.at(row_index).size()) {
            throw nb::index_error("row index out of range");
        }
        return this->frame_.at(row_index)[col_index];
    }

    bool cast() const noexcept {
        return this->cast_;
    }

    size_t frame_size_for_iterator(size_t generation) const {
        this->validate_generation(generation);
        return this->frame_.size();
    }

private:
    DataFrame<> frame_;
    std::vector<std::uint8_t> deleted_rows_;
    size_t pending_delete_count_ = 0;
    size_t generation_ = 0;
    bool cast_ = false;

    Py_ssize_t normalize_index(Py_ssize_t index) const {
        const Py_ssize_t row_count = static_cast<Py_ssize_t>(this->frame_.size());
        if (index < 0) {
            index += row_count;
        }

        if (index < 0 || index >= row_count) {
            throw nb::index_error("row index out of range");
        }

        return index;
    }

    void require_no_pending_deletes() const {
        if (this->has_pending_deletes()) {
            throw std::runtime_error(
                "CSVDocument has pending row deletions; call materialize_deletes() or discard_deletes() before iterating or fetching rows"
            );
        }
    }
};

size_t CSVDocumentRow::size() const {
    return this->document_->row_size(this->row_index_, this->generation_);
}

nb::object CSVDocumentRow::get_item(nb::handle key) const {
    if (PySlice_Check(key.ptr())) {
        return this->slice(key);
    }

    return ::field_to_python(this->cell_at(this->index_from_key(key)), this->document_->cast());
}

nb::iterator CSVDocumentRow::iter() const {
    return nb::make_iterator(nb::type<CSVDocumentRow>(), "iterator", FieldIterator(this, 0), FieldIterator(this, this->size()));
}

nb::list CSVDocumentRow::as_list() const {
    PyObject* raw_out = PyList_New(static_cast<Py_ssize_t>(this->size()));
    if (raw_out == nullptr) {
        throw nb::python_error();
    }

    nb::list out = nb::steal<nb::list>(raw_out);
    for (size_t i = 0; i < this->size(); ++i) {
        nb::object value = this->field_to_python(i);
        PyList_SET_ITEM(out.ptr(), static_cast<Py_ssize_t>(i), value.release().ptr());
    }
    return out;
}

nb::dict CSVDocumentRow::as_dict() const {
    const std::vector<std::string>& columns = this->get_col_names();
    nb::dict out;
    const size_t n = (std::min)(columns.size(), this->size());
    for (size_t i = 0; i < n; ++i) {
        out[nb::str(columns[i].data(), columns[i].size())] = this->field_to_python(i);
    }
    return out;
}

const std::vector<std::string>& CSVDocumentRow::get_col_names() const {
    return this->document_->columns();
}

bool CSVDocumentRow::remove() {
    return this->document_->mark_delete(this->row_index_, this->generation_);
}

std::string CSVDocumentRow::get_str(nb::handle key) const {
    return this->cell_at(this->index_from_key(key)).get<std::string>();
}

std::int64_t CSVDocumentRow::get_int(nb::handle key) const {
    return this->cell_at(this->index_from_key(key)).get<std::int64_t>();
}

double CSVDocumentRow::get_float(nb::handle key) const {
    return this->cell_at(this->index_from_key(key)).get<double>();
}

bool CSVDocumentRow::get_bool(nb::handle key) const {
    return this->cell_at(this->index_from_key(key)).get<bool>();
}

DataType CSVDocumentRow::type(nb::handle key) const {
    return this->cell_at(this->index_from_key(key)).type();
}

Py_ssize_t CSVDocumentRow::normalize_index(Py_ssize_t index) const {
    const Py_ssize_t row_size = static_cast<Py_ssize_t>(this->size());
    if (index < 0) {
        index += row_size;
    }

    if (index < 0 || index >= row_size) {
        throw nb::index_error("row index out of range");
    }

    return index;
}

size_t CSVDocumentRow::index_from_key(nb::handle key) const {
    if (PyIndex_Check(key.ptr())) {
        return static_cast<size_t>(this->normalize_index(PyNumber_AsSsize_t(key.ptr(), PyExc_IndexError)));
    }

    if (PyUnicode_Check(key.ptr())) {
        return this->index_of(nb::cast<std::string>(key));
    }

    throw nb::type_error("row accessor expects an integer index or column name");
}

size_t CSVDocumentRow::index_of(const std::string& col_name) const {
    const std::vector<std::string>& columns = this->get_col_names();
    const auto it = std::find(columns.begin(), columns.end(), col_name);
    if (it == columns.end()) {
        throw nb::index_error(("Can't find a column named " + col_name).c_str());
    }

    const size_t index = static_cast<size_t>(it - columns.begin());
    if (index >= this->size()) {
        throw nb::index_error("column index out of range");
    }
    return index;
}

DataFrameCell CSVDocumentRow::cell_at(size_t col_index) const {
    return this->document_->cell_at(this->row_index_, col_index, this->generation_);
}

nb::object CSVDocumentRow::field_to_python(size_t col_index) const {
    return ::field_to_python(this->cell_at(col_index), this->document_->cast());
}

nb::list CSVDocumentRow::slice(nb::handle key) const {
    Py_ssize_t start = 0;
    Py_ssize_t stop = 0;
    Py_ssize_t step = 0;
    Py_ssize_t slicelength = 0;
    if (PySlice_GetIndicesEx(key.ptr(), static_cast<Py_ssize_t>(this->size()), &start, &stop, &step, &slicelength) < 0) {
        throw nb::python_error();
    }

    PyObject* raw_out = PyList_New(slicelength);
    if (raw_out == nullptr) {
        throw nb::python_error();
    }

    nb::list out = nb::steal<nb::list>(raw_out);
    Py_ssize_t current = start;
    for (Py_ssize_t i = 0; i < slicelength; ++i) {
        nb::object value = this->field_to_python(static_cast<size_t>(current));
        PyList_SET_ITEM(out.ptr(), i, value.release().ptr());
        current += step;
    }
    return out;
}

CSVDocumentRow CSVDocumentIterator::next() {
    if (this->index_ >= this->document_->frame_size_for_iterator(this->generation_)) {
        throw nb::stop_iteration();
    }

    return CSVDocumentRow(this->document_, this->index_++, this->generation_);
}

void init_CSVDocument(nb::module_& m) {
    nb::class_<CSVDocumentRow>(m, "_DocumentRow")
    .def("__len__", &CSVDocumentRow::size)
    .def("__getitem__", &CSVDocumentRow::get_item, nb::is_operator())
    .def("__iter__", &CSVDocumentRow::iter, nb::keep_alive<0, 1>())
    .def("as_list", &CSVDocumentRow::as_list)
    .def("as_dict", &CSVDocumentRow::as_dict)
    .def("delete", &CSVDocumentRow::remove)
    .def("get_col_names", &CSVDocumentRow::get_col_names)
    .def("get_str", &CSVDocumentRow::get_str)
    .def("get_int", &CSVDocumentRow::get_int)
    .def("get_float", &CSVDocumentRow::get_float)
    .def("get_bool", &CSVDocumentRow::get_bool)
    .def("type", &CSVDocumentRow::type);

    nb::class_<CSVDocumentIterator>(m, "_CSVDocumentIterator")
    .def("__iter__",
        &CSVDocumentIterator::iter,
        nb::rv_policy::reference_internal)
    .def("__next__", &CSVDocumentIterator::next, nb::keep_alive<0, 1>());

    nb::class_<CSVDocumentNative>(m, "_CSVDocument")
    .def(nb::init<const std::string&, CSVFormat, bool>(),
        "filename"_a,
        "format"_a,
        "cast"_a = false)
    .def("__iter__", &CSVDocumentNative::iter, nb::keep_alive<0, 1>())
    .def("__getitem__", &CSVDocumentNative::at, nb::is_operator(), nb::keep_alive<0, 1>())
    .def("__len__", &CSVDocumentNative::size)
    .def_prop_ro("pending_deletes", &CSVDocumentNative::has_pending_deletes)
    .def("materialize_deletes", &CSVDocumentNative::materialize_deletes)
    .def("discard_deletes", &CSVDocumentNative::discard_deletes)
    .def("to_numpy",
        &CSVDocumentNative::to_numpy,
        "columns"_a = nb::none(),
        "cast"_a = true,
        "predicate"_a = nb::none())
    .def("delete_where",
        &CSVDocumentNative::delete_where,
        "predicate"_a)
    .def("filter",
        &CSVDocumentNative::filter,
        "predicate"_a);
}
