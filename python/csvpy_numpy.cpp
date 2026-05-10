#include "csvpy_bindings.hpp"
#include "csvpy_predicate.hpp"

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

static const size_t NUMPY_CHUNK_ROWS = 50000;

inline bool is_integral_type(DataType type) noexcept {
    return type >= DataType::CSV_INT8 && type <= DataType::CSV_INT64;
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

inline std::vector<size_t> selected_column_indices(const std::vector<NumpyColumnPlan>& plan) {
    std::vector<size_t> indices;
    indices.reserve(plan.size());
    for (const auto& column : plan) {
        indices.push_back(column.index);
    }
    return indices;
}

inline size_t numpy_worker_count(size_t selected_columns) noexcept {
#if CSV_ENABLE_THREADS
    if (selected_columns < 3) {
        return 0;
    }

    const unsigned int hardware_threads = std::thread::hardware_concurrency();
    const size_t max_workers = selected_columns - 1;
    if (hardware_threads == 0) {
        return max_workers;
    }

    return (std::min)(max_workers, static_cast<size_t>(hardware_threads));
#else
    (void)selected_columns;
    return 0;
#endif
}

template<typename State, typename Fn>
size_t chunk_selected_columns(
    CSVReader& reader,
    DataFrameExecutor& executor,
    const std::vector<size_t>& column_indices,
    std::vector<State>& states,
    Fn&& fn
) {
    std::vector<CSVRow> rows;
    size_t row_count = 0;
    while (reader.read_chunk(rows, NUMPY_CHUNK_ROWS)) {
        row_count += rows.size();
        DataFrame<> batch(std::move(rows));
        batch.column_parallel_apply(executor, column_indices, states, std::forward<Fn>(fn));
    }
    return row_count;
}

template<typename State, typename Fn>
void chunk_selected_columns_filtered(
    CSVReader& reader,
    DataFrameExecutor& executor,
    const std::vector<size_t>& column_indices,
    std::vector<State>& states,
    const RowPredicate* predicate,
    Fn&& fn
) {
    std::vector<CSVRow> rows;
    while (reader.read_chunk(rows, NUMPY_CHUNK_ROWS)) {
        DataFrame<> batch(std::move(rows));
        const std::vector<std::uint8_t> excluded_rows = excluded_rows_for_predicate(batch, {}, predicate);
        batch.column_parallel_apply(
            executor,
            column_indices,
            states,
            [&excluded_rows, &fn](DataFrame<>::column_type column, State& state) {
                fn(column, state, excluded_rows);
            }
        );
    }
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
    size_t seen = 0;
    bool replay_strings = false;

    explicit NumpyColumnBuffer(NumpyColumnPlan column)
        : plan(std::move(column)) {}

    void finalize() {
        if (this->plan.kind != NumpyColumnKind::UNKNOWN) {
            return;
        }

        this->plan.kind = NumpyColumnKind::FLOAT64;
        this->floats.reset(new std::vector<double>());
        this->floats->assign(this->seen, (std::numeric_limits<double>::quiet_NaN)());
    }

    void reset_for_string_replay() {
        this->plan.kind = NumpyColumnKind::STRING;
        this->plan.nullable = false;
        this->replay_strings = false;
        this->ints.reset();
        this->floats.reset();
        this->bools.reset();
        this->strings.clear();
        this->strings.reserve(this->seen);
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

    void append_casted(CSVField field) {
        if (this->replay_strings) {
            ++this->seen;
            return;
        }

        if (this->plan.kind == NumpyColumnKind::STRING) {
            this->append_replayed_string(field.get_sv());
            ++this->seen;
            return;
        }

        const DataType type = field.type();
        if (type == DataType::CSV_NULL) {
            this->append_null();
            return;
        }

        const NumpyColumnKind observed = this->kind_for_type(type);
        if (observed == NumpyColumnKind::STRING) {
            this->append_stringish(field.get_sv());
            return;
        }

        if (this->plan.kind == NumpyColumnKind::UNKNOWN) {
            this->start_typed_column(observed, field);
            return;
        }

        if (this->plan.kind == observed) {
            this->append_current_kind(field, observed);
            return;
        }

        if ((this->plan.kind == NumpyColumnKind::INT64 && observed == NumpyColumnKind::FLOAT64)
            || (this->plan.kind == NumpyColumnKind::FLOAT64 && observed == NumpyColumnKind::INT64)) {
            this->promote_to_float();
            this->plan.kind = NumpyColumnKind::FLOAT64;
            this->append_float_value(field, observed);
            return;
        }

        this->request_string_replay();
        ++this->seen;
    }

    void append_string(csv::string_view value) {
        this->strings.emplace_back(value.data(), value.size());
        ++this->seen;
    }

    void append_replayed_string(csv::string_view value) {
        this->strings.emplace_back(value.data(), value.size());
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

private:
    static NumpyColumnKind kind_for_type(DataType type) noexcept {
        if (is_integral_type(type)) {
            return NumpyColumnKind::INT64;
        }
        if (type == DataType::CSV_DOUBLE) {
            return NumpyColumnKind::FLOAT64;
        }
        if (type == DataType::CSV_BOOL) {
            return NumpyColumnKind::BOOL;
        }
        return NumpyColumnKind::STRING;
    }

    void append_null() {
        this->plan.nullable = true;
        if (this->plan.kind == NumpyColumnKind::UNKNOWN) {
            ++this->seen;
            return;
        }

        if (this->plan.kind == NumpyColumnKind::STRING) {
            this->strings.push_back(std::string());
            ++this->seen;
            return;
        }

        this->promote_to_float();
        this->floats->push_back((std::numeric_limits<double>::quiet_NaN)());
        ++this->seen;
    }

    void append_stringish(csv::string_view text) {
        if (this->plan.kind == NumpyColumnKind::UNKNOWN) {
            this->plan.kind = NumpyColumnKind::STRING;
            this->strings.assign(this->seen, std::string());
            this->append_string(text);
            return;
        }

        if (this->plan.kind == NumpyColumnKind::STRING) {
            this->append_string(text);
            return;
        }

        this->request_string_replay();
        ++this->seen;
    }

    void start_typed_column(NumpyColumnKind observed, CSVField field) {
        this->plan.kind = observed;
        if (this->plan.nullable || observed == NumpyColumnKind::FLOAT64) {
            this->floats.reset(new std::vector<double>());
            this->floats->assign(this->seen, (std::numeric_limits<double>::quiet_NaN)());
            this->append_float_value(field, observed);
            return;
        }

        if (observed == NumpyColumnKind::INT64) {
            this->ints.reset(new std::vector<std::int64_t>());
            this->ints->push_back(field.get<std::int64_t>());
        }
        else {
            this->bools.reset(new std::vector<std::uint8_t>());
            this->bools->push_back(field.get<bool>() ? 1 : 0);
        }
        ++this->seen;
    }

    void append_current_kind(CSVField field, NumpyColumnKind observed) {
        if (this->storage_kind() == NumpyColumnKind::FLOAT64) {
            this->append_float_value(field, observed);
            return;
        }

        if (observed == NumpyColumnKind::INT64) {
            this->ints->push_back(field.get<std::int64_t>());
        }
        else {
            this->bools->push_back(field.get<bool>() ? 1 : 0);
        }
        ++this->seen;
    }

    void append_float_value(CSVField field, NumpyColumnKind observed) {
        if (!this->floats) {
            this->floats.reset(new std::vector<double>());
        }

        if (observed == NumpyColumnKind::BOOL) {
            this->floats->push_back(field.get<bool>() ? 1.0 : 0.0);
        }
        else {
            this->floats->push_back(field.get<double>());
        }
        ++this->seen;
    }

    void promote_to_float() {
        if (this->floats) {
            return;
        }

        std::unique_ptr<std::vector<double>> promoted(new std::vector<double>());
        promoted->reserve(this->seen + 1);
        if (this->ints) {
            for (const auto value : *this->ints) {
                promoted->push_back(static_cast<double>(value));
            }
            this->ints.reset();
        }
        else if (this->bools) {
            for (const auto value : *this->bools) {
                promoted->push_back(value ? 1.0 : 0.0);
            }
            this->bools.reset();
        }
        this->floats = std::move(promoted);
    }

    void request_string_replay() {
        this->plan.kind = NumpyColumnKind::STRING;
        this->plan.nullable = false;
        this->replay_strings = true;
        this->ints.reset();
        this->floats.reset();
        this->bools.reset();
        this->strings.clear();
    }
};

inline void append_numpy_column(DataFrame<>::column_type column, NumpyColumnBuffer& buffer) {
    for (size_t row_index = 0; row_index < column.size(); ++row_index) {
        if (buffer.storage_kind() == NumpyColumnKind::STRING && !buffer.replay_strings) {
            buffer.append_string(column.get_sv(row_index));
        }
        else {
            buffer.append_casted(column[row_index]);
        }
    }
}

inline void replay_numpy_string_column(DataFrame<>::column_type column, NumpyColumnBuffer*& buffer) {
    for (size_t row_index = 0; row_index < column.size(); ++row_index) {
        buffer->append_replayed_string(column.get_sv(row_index));
    }
}

inline bool needs_string_replay(const std::vector<NumpyColumnBuffer>& buffers) {
    for (const auto& buffer : buffers) {
        if (buffer.replay_strings) {
            return true;
        }
    }
    return false;
}

inline void replay_string_columns(
    const std::string& filename,
    const CSVFormat& format,
    DataFrameExecutor& executor,
    std::vector<NumpyColumnBuffer>& buffers
) {
    std::vector<size_t> replay_indices;
    std::vector<NumpyColumnBuffer*> replay_buffers;
    for (auto& buffer : buffers) {
        if (buffer.replay_strings) {
            replay_indices.push_back(buffer.plan.index);
            buffer.reset_for_string_replay();
            replay_buffers.push_back(&buffer);
        }
    }

    if (replay_indices.empty()) {
        return;
    }

    CSVReader replay_reader(filename, format);
    chunk_selected_columns(replay_reader, executor, replay_indices, replay_buffers, replay_numpy_string_column);
}

inline void replay_numpy_string_column_filtered(
    DataFrame<>::column_type column,
    NumpyColumnBuffer*& buffer,
    const std::vector<std::uint8_t>& deleted_rows
);

inline void replay_string_columns_filtered(
    const std::string& filename,
    const CSVFormat& format,
    DataFrameExecutor& executor,
    std::vector<NumpyColumnBuffer>& buffers,
    const RowPredicate* predicate
) {
    std::vector<size_t> replay_indices;
    std::vector<NumpyColumnBuffer*> replay_buffers;
    for (auto& buffer : buffers) {
        if (buffer.replay_strings) {
            replay_indices.push_back(buffer.plan.index);
            buffer.reset_for_string_replay();
            replay_buffers.push_back(&buffer);
        }
    }

    if (replay_indices.empty()) {
        return;
    }

    CSVReader replay_reader(filename, format);
    chunk_selected_columns_filtered(
        replay_reader,
        executor,
        replay_indices,
        replay_buffers,
        predicate,
        replay_numpy_string_column_filtered
    );
}

inline void finalize_numpy_buffers(std::vector<NumpyColumnBuffer>& buffers) {
    for (auto& buffer : buffers) {
        buffer.finalize();
    }
}

inline void append_numpy_strings_only(DataFrame<>::column_type column, NumpyColumnBuffer& buffer) {
    if (buffer.plan.kind == NumpyColumnKind::UNKNOWN) {
        buffer.plan.kind = NumpyColumnKind::STRING;
    }

    for (size_t row_index = 0; row_index < column.size(); ++row_index) {
        buffer.append_string(column.get_sv(row_index));
    }
}

inline bool row_is_deleted(const std::vector<std::uint8_t>& deleted_rows, size_t row_index) {
    return row_index < deleted_rows.size() && deleted_rows[row_index] != 0;
}

inline void append_numpy_column_filtered(
    DataFrame<>::column_type column,
    NumpyColumnBuffer& buffer,
    const std::vector<std::uint8_t>& deleted_rows
) {
    for (size_t row_index = 0; row_index < column.size(); ++row_index) {
        if (row_is_deleted(deleted_rows, row_index)) {
            continue;
        }

        if (buffer.storage_kind() == NumpyColumnKind::STRING && !buffer.replay_strings) {
            buffer.append_string(column.get_sv(row_index));
        }
        else {
            buffer.append_casted(column[row_index]);
        }
    }
}

inline void replay_numpy_string_column_filtered(
    DataFrame<>::column_type column,
    NumpyColumnBuffer*& buffer,
    const std::vector<std::uint8_t>& deleted_rows
) {
    for (size_t row_index = 0; row_index < column.size(); ++row_index) {
        if (!row_is_deleted(deleted_rows, row_index)) {
            buffer->append_replayed_string(column.get_sv(row_index));
        }
    }
}

inline void append_numpy_strings_only_filtered(
    DataFrame<>::column_type column,
    NumpyColumnBuffer& buffer,
    const std::vector<std::uint8_t>& deleted_rows
) {
    if (buffer.plan.kind == NumpyColumnKind::UNKNOWN) {
        buffer.plan.kind = NumpyColumnKind::STRING;
    }

    for (size_t row_index = 0; row_index < column.size(); ++row_index) {
        if (!row_is_deleted(deleted_rows, row_index)) {
            buffer.append_string(column.get_sv(row_index));
        }
    }
}

inline void replay_string_columns_from_frame(
    const DataFrame<>& frame,
    DataFrameExecutor& executor,
    std::vector<NumpyColumnBuffer>& buffers,
    const std::vector<std::uint8_t>& deleted_rows
) {
    std::vector<size_t> replay_indices;
    std::vector<NumpyColumnBuffer*> replay_buffers;
    for (auto& buffer : buffers) {
        if (buffer.replay_strings) {
            replay_indices.push_back(buffer.plan.index);
            buffer.reset_for_string_replay();
            replay_buffers.push_back(&buffer);
        }
    }

    if (replay_indices.empty()) {
        return;
    }

    frame.column_parallel_apply(
        executor,
        replay_indices,
        replay_buffers,
        [&deleted_rows](DataFrame<>::column_type column, NumpyColumnBuffer*& buffer) {
            replay_numpy_string_column_filtered(column, buffer, deleted_rows);
        }
    );
}

nb::dict read_numpy(const std::string& filename, nb::object columns, bool cast, nb::object predicate) {
    CSVFormat format = CSVFormat::guess_csv();
    if (cast) {
        format.eager_field_classification();
    }

    CSVReader header_reader(filename, format);
    std::vector<NumpyColumnPlan> plan = make_numpy_column_plan(header_reader.get_col_names(), columns);
    const std::vector<size_t> selected_indices = selected_column_indices(plan);
    DataFrameExecutor executor(numpy_worker_count(selected_indices.size()));
    const RowPredicate* row_predicate = optional_row_predicate(predicate);

    if (!cast) {
        for (auto& column : plan) {
            column.kind = NumpyColumnKind::STRING;
        }
    }

    std::vector<NumpyColumnBuffer> buffers;
    buffers.reserve(plan.size());
    for (auto& column : plan) {
        buffers.emplace_back(std::move(column));
    }

    if (cast) {
        if (row_predicate) {
            chunk_selected_columns_filtered(
                header_reader,
                executor,
                selected_indices,
                buffers,
                row_predicate,
                append_numpy_column_filtered
            );
        }
        else {
            chunk_selected_columns(header_reader, executor, selected_indices, buffers, append_numpy_column);
        }
        if (needs_string_replay(buffers)) {
            if (row_predicate) {
                replay_string_columns_filtered(filename, format, executor, buffers, row_predicate);
            }
            else {
                replay_string_columns(filename, format, executor, buffers);
            }
        }
        finalize_numpy_buffers(buffers);
    }
    else {
        if (row_predicate) {
            chunk_selected_columns_filtered(
                header_reader,
                executor,
                selected_indices,
                buffers,
                row_predicate,
                append_numpy_strings_only_filtered
            );
        }
        else {
            chunk_selected_columns(header_reader, executor, selected_indices, buffers, append_numpy_strings_only);
        }
    }

    nb::dict out;
    for (auto& buffer : buffers) {
        out[nb::str(buffer.plan.name.data(), buffer.plan.name.size())] = buffer.into_python();
    }
    return out;
}

nb::dict data_frame_to_numpy(
    const DataFrame<>& frame,
    const std::vector<std::uint8_t>& deleted_rows,
    nb::object columns,
    bool cast,
    nb::object predicate
) {
    std::vector<NumpyColumnPlan> plan = make_numpy_column_plan(frame.columns(), columns);
    const std::vector<size_t> selected_indices = selected_column_indices(plan);
    DataFrameExecutor executor(numpy_worker_count(selected_indices.size()));
    const RowPredicate* row_predicate = optional_row_predicate(predicate);
    const std::vector<std::uint8_t> excluded_rows = excluded_rows_for_predicate(frame, deleted_rows, row_predicate);

    if (!cast) {
        for (auto& column : plan) {
            column.kind = NumpyColumnKind::STRING;
        }
    }

    std::vector<NumpyColumnBuffer> buffers;
    buffers.reserve(plan.size());
    for (auto& column : plan) {
        buffers.emplace_back(std::move(column));
    }

    if (cast) {
        frame.column_parallel_apply(
            executor,
            selected_indices,
            buffers,
            [&excluded_rows](DataFrame<>::column_type column, NumpyColumnBuffer& buffer) {
                append_numpy_column_filtered(column, buffer, excluded_rows);
            }
        );
        if (needs_string_replay(buffers)) {
            replay_string_columns_from_frame(frame, executor, buffers, excluded_rows);
        }
        finalize_numpy_buffers(buffers);
    }
    else {
        frame.column_parallel_apply(
            executor,
            selected_indices,
            buffers,
            [&excluded_rows](DataFrame<>::column_type column, NumpyColumnBuffer& buffer) {
                append_numpy_strings_only_filtered(column, buffer, excluded_rows);
            }
        );
    }

    nb::dict out;
    for (auto& buffer : buffers) {
        out[nb::str(buffer.plan.name.data(), buffer.plan.name.size())] = buffer.into_python();
    }
    return out;
}
