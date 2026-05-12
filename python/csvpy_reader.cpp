#include "csvpy_bindings.hpp"
#include "csvpy_predicate.hpp"

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

    nb::list as_list(nb::object selected = nb::none()) const {
        if (selected.is_none()) {
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

        nb::object selected_columns = this->selected_column_sequence(selected);
        const Py_ssize_t selected_size = PySequence_Fast_GET_SIZE(selected_columns.ptr());
        PyObject* raw_out = PyList_New(selected_size);
        if (raw_out == nullptr) {
            throw nb::python_error();
        }

        nb::list out = nb::steal<nb::list>(raw_out);
        PyObject** items = PySequence_Fast_ITEMS(selected_columns.ptr());
        for (Py_ssize_t i = 0; i < selected_size; ++i) {
            const ColumnView column = this->column_view(items[i]);
            nb::object value = this->field_to_python(this->field_named(column));
            PyList_SET_ITEM(out.ptr(), i, value.release().ptr());
        }
        return out;
    }

    nb::tuple as_tuple(nb::object selected = nb::none()) const {
        if (selected.is_none()) {
            PyObject* raw_out = PyTuple_New(static_cast<Py_ssize_t>(this->row_.size()));
            if (raw_out == nullptr) {
                throw nb::python_error();
            }

            nb::tuple out = nb::steal<nb::tuple>(raw_out);
            bool has_tracked_item = false;
            for (size_t i = 0; i < this->row_.size(); ++i) {
                nb::object value = this->field_to_python(this->row_[i]);
                has_tracked_item = has_tracked_item || this->is_gc_tracked(value.ptr());
                PyTuple_SET_ITEM(out.ptr(), static_cast<Py_ssize_t>(i), value.release().ptr());
            }
            this->untrack_tuple_if_possible(out.ptr(), has_tracked_item);
            return out;
        }

        nb::object selected_columns = this->selected_column_sequence(selected);
        const Py_ssize_t selected_size = PySequence_Fast_GET_SIZE(selected_columns.ptr());
        PyObject* raw_out = PyTuple_New(selected_size);
        if (raw_out == nullptr) {
            throw nb::python_error();
        }

        nb::tuple out = nb::steal<nb::tuple>(raw_out);
        PyObject** items = PySequence_Fast_ITEMS(selected_columns.ptr());
        bool has_tracked_item = false;
        for (Py_ssize_t i = 0; i < selected_size; ++i) {
            const ColumnView column = this->column_view(items[i]);
            nb::object value = this->field_to_python(this->field_named(column));
            has_tracked_item = has_tracked_item || this->is_gc_tracked(value.ptr());
            PyTuple_SET_ITEM(out.ptr(), i, value.release().ptr());
        }
        this->untrack_tuple_if_possible(out.ptr(), has_tracked_item);
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

        nb::object selected_columns = this->selected_column_sequence(selected);
        PyObject** items = PySequence_Fast_ITEMS(selected_columns.ptr());
        const Py_ssize_t selected_size = PySequence_Fast_GET_SIZE(selected_columns.ptr());
        for (Py_ssize_t i = 0; i < selected_size; ++i) {
            const ColumnView column = this->column_view(items[i]);
            out[nb::str(column.data, static_cast<size_t>(column.size))] = this->field_to_python(this->field_named(column));
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

    struct ColumnView {
        const char* data;
        Py_ssize_t size;
    };

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

    CSVField field_named(ColumnView col_name) const {
        try {
            return this->row_[csv::string_view(col_name.data, static_cast<size_t>(col_name.size))];
        }
        catch (const std::exception&) {
            std::string message("Can't find a column named ");
            message.append(col_name.data, static_cast<size_t>(col_name.size));
            throw nb::index_error(message.c_str());
        }
    }

    nb::object selected_column_sequence(nb::object selected) const {
        PyObject* raw_selected = PySequence_Fast(selected.ptr(), "columns must be a sequence of strings");
        if (raw_selected == nullptr) {
            throw nb::python_error();
        }
        return nb::steal<nb::object>(raw_selected);
    }

    ColumnView column_view(PyObject* column) const {
        if (!PyUnicode_Check(column)) {
            throw nb::type_error("columns must be strings");
        }

        Py_ssize_t size = 0;
        const char* data = PyUnicode_AsUTF8AndSize(column, &size);
        if (data == nullptr) {
            throw nb::python_error();
        }

        return { data, size };
    }

    bool is_gc_tracked(PyObject* obj) const noexcept {
        return PyObject_GC_IsTracked(obj) != 0;
    }

    void untrack_tuple_if_possible(PyObject* tuple, bool has_tracked_item) const noexcept {
        if (!has_tracked_item && this->is_gc_tracked(tuple)) {
            PyObject_GC_UnTrack(tuple);
        }
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
private:
    enum class MaterializationKind {
        LIST,
        TUPLE,
        DICT
    };

    struct ColumnSelection {
        std::vector<std::string> names;
        std::vector<size_t> indices;
        bool all_columns = true;
    };

public:
    class MaterializedRows {
    public:
        MaterializedRows(
            LazyCSVRowReader* reader,
            MaterializationKind kind,
            ColumnSelection columns
        ) : reader_(reader),
            kind_(kind),
            columns_(std::move(columns)) {}

        MaterializedRows& iter() {
            return *this;
        }

        nb::object next() {
            return this->reader_->next_materialized(this->kind_, this->columns_);
        }

        nb::list read_chunk(size_t size) {
            if (size == 0) {
                throw nb::value_error("chunk size must be greater than zero");
            }
            return this->reader_->materialized_chunk(this->kind_, this->columns_, size);
        }

        nb::list all() {
            return this->reader_->materialized_all(this->kind_, this->columns_);
        }

    private:
        LazyCSVRowReader* reader_;
        MaterializationKind kind_;
        ColumnSelection columns_;
    };

    LazyCSVRowReader(
        const std::string& filename,
        CSVFormat format,
        bool cast,
        size_t batch_size = 8192
    ) : reader_(filename, cast ? format.eager_field_classification() : format),
        cast_(cast),
        batch_size_(batch_size == 0 ? 1 : batch_size) {}

    LazyCSVRowReader& iter() {
        return *this;
    }

    const std::vector<std::string>& get_col_names() const {
        return this->reader_.get_col_names();
    }

    LazyCSVRow next() {
        CSVRow row;
        if (!this->read_next_row(row)) {
            throw nb::stop_iteration();
        }

        return LazyCSVRow(std::move(row), this->cast_);
    }

    LazyCSVRowReader& filter(nb::object predicate, bool append = true) {
        const RowPredicate* row_predicate = optional_row_predicate(predicate);
        if (row_predicate == nullptr) {
            this->predicate_.reset();
            this->pending_rows_.clear();
            this->pending_index_ = 0;
            return *this;
        }

        const auto& column_names = this->reader_.get_col_names();
        row_predicate->validate_columns(column_names);

        if (append && this->predicate_) {
            std::vector<RowPredicate> predicates;
            predicates.reserve(2);
            predicates.push_back(*this->predicate_);
            predicates.push_back(*row_predicate);
            this->predicate_.reset(new RowPredicate(std::move(predicates)));
        }
        else {
            this->predicate_.reset(new RowPredicate(*row_predicate));
        }

        this->pending_rows_.clear();
        this->pending_index_ = 0;
        return *this;
    }

    MaterializedRows lists(nb::object selected = nb::none()) {
        return MaterializedRows(this, MaterializationKind::LIST, this->column_selection(selected, false));
    }

    MaterializedRows tuples(nb::object selected = nb::none()) {
        return MaterializedRows(this, MaterializationKind::TUPLE, this->column_selection(selected, false));
    }

    MaterializedRows dicts(nb::object selected = nb::none()) {
        return MaterializedRows(this, MaterializationKind::DICT, this->column_selection(selected, true));
    }

    nb::list to_lists(nb::object selected = nb::none()) {
        const ColumnSelection columns = this->column_selection(selected, false);
        return this->materialized_all(MaterializationKind::LIST, columns);
    }

    nb::list to_tuples(nb::object selected = nb::none()) {
        const ColumnSelection columns = this->column_selection(selected, false);
        return this->materialized_all(MaterializationKind::TUPLE, columns);
    }

    nb::list to_dicts(nb::object selected = nb::none()) {
        const ColumnSelection columns = this->column_selection(selected, true);
        return this->materialized_all(MaterializationKind::DICT, columns);
    }

private:
    CSVReader reader_;
    bool cast_ = false;
    size_t batch_size_ = 8192;
    std::unique_ptr<RowPredicate> predicate_;
    std::vector<CSVRow> pending_rows_;
    size_t pending_index_ = 0;

    bool read_next_row(CSVRow& row) {
        if (!this->predicate_) {
            return this->reader_.read_row(row);
        }

        while (true) {
            if (this->pending_index_ < this->pending_rows_.size()) {
                row = std::move(this->pending_rows_[this->pending_index_]);
                ++this->pending_index_;
                if (this->pending_index_ == this->pending_rows_.size()) {
                    this->pending_rows_.clear();
                    this->pending_index_ = 0;
                }
                return true;
            }

            if (!this->read_filtered_chunk()) {
                return false;
            }
        }
    }

    bool read_filtered_chunk() {
        std::vector<CSVRow> rows;
        while (this->reader_.read_chunk(rows, this->batch_size_)) {
            DataFrame<> batch(rows);
            const std::vector<std::uint8_t> included_rows =
                included_rows_for_predicate(batch, {}, *this->predicate_);

            this->pending_rows_.clear();
            this->pending_rows_.reserve(rows.size());
            for (size_t row_index = 0; row_index < rows.size(); ++row_index) {
                if (included_rows[row_index]) {
                    this->pending_rows_.push_back(std::move(rows[row_index]));
                }
            }
            this->pending_index_ = 0;

            if (!this->pending_rows_.empty()) {
                return true;
            }
        }

        return false;
    }

    nb::object materialize_row(
        const CSVRow& row,
        MaterializationKind kind,
        const ColumnSelection& columns
    ) const {
        switch (kind) {
        case MaterializationKind::LIST:
            return this->row_to_list(row, columns);
        case MaterializationKind::TUPLE:
            return this->row_to_tuple(row, columns);
        case MaterializationKind::DICT:
            return this->row_to_dict(row, columns);
        }

        throw std::runtime_error("unknown row materialization kind");
    }

    nb::object next_materialized(MaterializationKind kind, const ColumnSelection& columns) {
        CSVRow row;
        if (!this->read_next_row(row)) {
            throw nb::stop_iteration();
        }
        return this->materialize_row(row, kind, columns);
    }

    nb::list materialized_chunk(
        MaterializationKind kind,
        const ColumnSelection& columns,
        size_t max_rows
    ) {
        nb::list out = this->empty_list();
        CSVRow row;
        size_t rows_read = 0;
        while (rows_read < max_rows && this->read_next_row(row)) {
            nb::object materialized = this->materialize_row(row, kind, columns);
            this->append_list(out, materialized);
            ++rows_read;
        }
        return out;
    }

    nb::list materialized_all(MaterializationKind kind, const ColumnSelection& columns) {
        nb::list out = this->empty_list();
        CSVRow row;
        while (this->read_next_row(row)) {
            nb::object materialized = this->materialize_row(row, kind, columns);
            this->append_list(out, materialized);
        }
        return out;
    }

    nb::object field_to_python(CSVField field) const {
        return ::field_to_python(std::move(field), this->cast_);
    }

    nb::list empty_list() const {
        PyObject* raw_out = PyList_New(0);
        if (raw_out == nullptr) {
            throw nb::python_error();
        }
        return nb::steal<nb::list>(raw_out);
    }

    void append_list(nb::list& out, const nb::object& item) const {
        if (PyList_Append(out.ptr(), item.ptr()) < 0) {
            throw nb::python_error();
        }
    }

    ColumnSelection column_selection(nb::object selected, bool require_names) const {
        const auto& names = this->reader_.get_col_names();
        if (selected.is_none()) {
            if (require_names && names.empty()) {
                throw nb::value_error("reader has no column names");
            }

            return { names, {}, true };
        }

        if (names.empty()) {
            throw nb::value_error("selected columns require column names");
        }

        PyObject* raw_selected = PySequence_Fast(selected.ptr(), "columns must be a sequence of strings");
        if (raw_selected == nullptr) {
            throw nb::python_error();
        }

        nb::object selected_sequence = nb::steal<nb::object>(raw_selected);
        PyObject** items = PySequence_Fast_ITEMS(selected_sequence.ptr());
        const Py_ssize_t selected_size = PySequence_Fast_GET_SIZE(selected_sequence.ptr());

        ColumnSelection out;
        out.all_columns = false;
        out.names.reserve(static_cast<size_t>(selected_size));
        out.indices.reserve(static_cast<size_t>(selected_size));

        for (Py_ssize_t i = 0; i < selected_size; ++i) {
            if (!PyUnicode_Check(items[i])) {
                throw nb::type_error("columns must be strings");
            }

            Py_ssize_t size = 0;
            const char* data = PyUnicode_AsUTF8AndSize(items[i], &size);
            if (data == nullptr) {
                throw nb::python_error();
            }

            std::string column(data, static_cast<size_t>(size));
            const auto it = std::find(names.begin(), names.end(), column);
            if (it == names.end()) {
                throw nb::index_error(("Can't find a column named " + column).c_str());
            }

            out.indices.push_back(static_cast<size_t>(it - names.begin()));
            out.names.push_back(std::move(column));
        }

        return out;
    }

    size_t output_size(const CSVRow& row, const ColumnSelection& columns) const noexcept {
        return columns.all_columns ? row.size() : columns.indices.size();
    }

    CSVField selected_field(const CSVRow& row, const ColumnSelection& columns, size_t out_index) const {
        const size_t row_index = columns.all_columns ? out_index : columns.indices[out_index];
        if (row_index >= row.size()) {
            throw nb::index_error("column index out of range");
        }
        return row[row_index];
    }

    nb::list row_to_list(const CSVRow& row, const ColumnSelection& columns) const {
        const size_t count = this->output_size(row, columns);
        PyObject* raw_out = PyList_New(static_cast<Py_ssize_t>(count));
        if (raw_out == nullptr) {
            throw nb::python_error();
        }

        nb::list out = nb::steal<nb::list>(raw_out);
        for (size_t i = 0; i < count; ++i) {
            nb::object value = this->field_to_python(this->selected_field(row, columns, i));
            PyList_SET_ITEM(out.ptr(), static_cast<Py_ssize_t>(i), value.release().ptr());
        }
        return out;
    }

    nb::tuple row_to_tuple(const CSVRow& row, const ColumnSelection& columns) const {
        const size_t count = this->output_size(row, columns);
        PyObject* raw_out = PyTuple_New(static_cast<Py_ssize_t>(count));
        if (raw_out == nullptr) {
            throw nb::python_error();
        }

        nb::tuple out = nb::steal<nb::tuple>(raw_out);
        bool has_tracked_item = false;
        for (size_t i = 0; i < count; ++i) {
            nb::object value = this->field_to_python(this->selected_field(row, columns, i));
            has_tracked_item = has_tracked_item || PyObject_GC_IsTracked(value.ptr()) != 0;
            PyTuple_SET_ITEM(out.ptr(), static_cast<Py_ssize_t>(i), value.release().ptr());
        }
        if (!has_tracked_item && PyObject_GC_IsTracked(out.ptr()) != 0) {
            PyObject_GC_UnTrack(out.ptr());
        }
        return out;
    }

    nb::dict row_to_dict(const CSVRow& row, const ColumnSelection& columns) const {
        const auto& names = columns.all_columns ? this->reader_.get_col_names() : columns.names;
        const size_t count = columns.all_columns ? (std::min)(names.size(), row.size()) : columns.indices.size();
        nb::dict out;
        for (size_t i = 0; i < count; ++i) {
            const std::string& name = names[i];
            out[nb::str(name.data(), name.size())] = this->field_to_python(this->selected_field(row, columns, i));
        }
        return out;
    }
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
    .def("as_list", &LazyCSVRow::as_list, "columns"_a = nb::none())
    .def("as_tuple", &LazyCSVRow::as_tuple, "columns"_a = nb::none())
    .def("as_dict", &LazyCSVRow::as_dict, "columns"_a = nb::none())
    .def("get_col_names", &LazyCSVRow::get_col_names)
    .def("get_str", &LazyCSVRow::get_str)
    .def("get_int", &LazyCSVRow::get_int)
    .def("get_float", &LazyCSVRow::get_float)
    .def("get_bool", &LazyCSVRow::get_bool)
    .def("type", &LazyCSVRow::type);

    nb::class_<LazyCSVRowReader::MaterializedRows>(m, "_MaterializedRows")
    .def("__iter__",
        &LazyCSVRowReader::MaterializedRows::iter,
        nb::rv_policy::reference_internal)
    .def("__next__", &LazyCSVRowReader::MaterializedRows::next)
    .def("read_chunk", &LazyCSVRowReader::MaterializedRows::read_chunk, "size"_a)
    .def("all", &LazyCSVRowReader::MaterializedRows::all);

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
    .def("__next__", &LazyCSVRowReader::next)
    .def("filter",
        &LazyCSVRowReader::filter,
        nb::rv_policy::reference_internal,
        "predicate"_a,
        "append"_a = true)
    .def("lists", &LazyCSVRowReader::lists, "columns"_a = nb::none())
    .def("tuples", &LazyCSVRowReader::tuples, "columns"_a = nb::none())
    .def("dicts", &LazyCSVRowReader::dicts, "columns"_a = nb::none())
    .def("to_lists", &LazyCSVRowReader::to_lists, "columns"_a = nb::none())
    .def("to_tuples", &LazyCSVRowReader::to_tuples, "columns"_a = nb::none())
    .def("to_dicts", &LazyCSVRowReader::to_dicts, "columns"_a = nb::none());
}
