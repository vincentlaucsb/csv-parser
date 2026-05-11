#include "csvpy_bindings.hpp"
#include "csvpy_predicate.hpp"

enum class NumpyColumnKind {
    UNKNOWN,
    INT64,
    FLOAT64,
    BOOL,
    STRING
};

enum class NumpyBatchSchemaMode {
    SAMPLE,
    BATCH,
    GLOBAL
};

struct NumpyColumnPlan {
    std::string name;
    size_t index = 0;
    NumpyColumnKind kind = NumpyColumnKind::UNKNOWN;
    bool nullable = false;
};

static const size_t NUMPY_CHUNK_ROWS = 50000;

inline NumpyBatchSchemaMode parse_numpy_batch_schema(std::string schema) {
    if (schema == "sample") {
        return NumpyBatchSchemaMode::SAMPLE;
    }
    if (schema == "batch") {
        return NumpyBatchSchemaMode::BATCH;
    }
    if (schema == "global") {
        return NumpyBatchSchemaMode::GLOBAL;
    }

    throw std::invalid_argument("read_numpy_batches() schema must be 'sample', 'batch', or 'global'");
}

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
        if (this->plan.kind == NumpyColumnKind::UNKNOWN) {
            this->plan.kind = NumpyColumnKind::FLOAT64;
            this->floats.reset(new std::vector<double>());
            this->floats->assign(this->seen, (std::numeric_limits<double>::quiet_NaN)());
            return;
        }

        switch (this->storage_kind()) {
        case NumpyColumnKind::INT64:
            if (!this->ints) {
                this->ints.reset(new std::vector<std::int64_t>());
            }
            break;
        case NumpyColumnKind::BOOL:
            if (!this->bools) {
                this->bools.reset(new std::vector<std::uint8_t>());
            }
            break;
        case NumpyColumnKind::FLOAT64:
            if (!this->floats) {
                this->floats.reset(new std::vector<double>());
                this->floats->assign(this->seen, (std::numeric_limits<double>::quiet_NaN)());
            }
            break;
        case NumpyColumnKind::STRING:
        case NumpyColumnKind::UNKNOWN:
            break;
        }
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

    void append_casted(csv::string_view text) {
        if (this->replay_strings) {
            ++this->seen;
            return;
        }

        if (this->plan.kind == NumpyColumnKind::STRING) {
            this->append_replayed_string(text);
            ++this->seen;
            return;
        }

        const csv::internals::CSVFieldScalar scalar = csv::internals::classify_field_scalar(text);
        const DataType type = scalar.type;
        if (type == DataType::CSV_NULL) {
            this->append_null();
            return;
        }

        const NumpyColumnKind observed = this->kind_for_type(type);
        if (observed == NumpyColumnKind::STRING) {
            this->append_stringish(text);
            return;
        }

        if (this->plan.kind == NumpyColumnKind::UNKNOWN) {
            this->start_typed_column(observed, scalar);
            return;
        }

        if (this->plan.kind == observed) {
            this->append_current_kind(scalar, observed);
            return;
        }

        if ((this->plan.kind == NumpyColumnKind::INT64 && observed == NumpyColumnKind::FLOAT64)
            || (this->plan.kind == NumpyColumnKind::FLOAT64 && observed == NumpyColumnKind::INT64)) {
            this->promote_to_float();
            this->plan.kind = NumpyColumnKind::FLOAT64;
            this->append_float_value(scalar, observed);
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

    void append_fixed_casted(csv::string_view text) {
        if (this->plan.kind == NumpyColumnKind::UNKNOWN) {
            this->append_casted(text);
            return;
        }

        switch (this->storage_kind()) {
        case NumpyColumnKind::STRING:
            this->append_string(text);
            return;
        case NumpyColumnKind::UNKNOWN:
            return;
        case NumpyColumnKind::INT64: {
            const csv::internals::CSVFieldScalar scalar = csv::internals::classify_field_scalar(text);
            if (!this->ints) {
                this->ints.reset(new std::vector<std::int64_t>());
            }

            this->ints->push_back(is_integral_type(scalar.type) ? scalar.integer : 0);
            ++this->seen;
            return;
        }
        case NumpyColumnKind::BOOL: {
            const csv::internals::CSVFieldScalar scalar = csv::internals::classify_field_scalar(text);
            if (!this->bools) {
                this->bools.reset(new std::vector<std::uint8_t>());
            }

            this->bools->push_back(scalar.type == DataType::CSV_BOOL && scalar.boolean ? 1 : 0);
            ++this->seen;
            return;
        }
        case NumpyColumnKind::FLOAT64: {
            const csv::internals::CSVFieldScalar scalar = csv::internals::classify_field_scalar(text);
            if (!this->floats) {
                this->floats.reset(new std::vector<double>());
            }

            if (scalar.type == DataType::CSV_NULL) {
                this->floats->push_back((std::numeric_limits<double>::quiet_NaN)());
            }
            else if (scalar.type == DataType::CSV_BOOL) {
                this->floats->push_back(scalar.boolean ? 1.0 : 0.0);
            }
            else if (is_integral_type(scalar.type)) {
                this->floats->push_back(static_cast<double>(scalar.integer));
            }
            else if (scalar.type == DataType::CSV_DOUBLE) {
                this->floats->push_back(static_cast<double>(scalar.floating));
            }
            else {
                this->floats->push_back((std::numeric_limits<double>::quiet_NaN)());
            }
            ++this->seen;
            return;
        }
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

private:
    static NumpyColumnKind kind_for_type(DataType type) noexcept {
        static const NumpyColumnKind type_to_kind[] = {
            NumpyColumnKind::STRING,  // CSV_NULL
            NumpyColumnKind::STRING,  // CSV_STRING
            NumpyColumnKind::BOOL,    // CSV_BOOL
            NumpyColumnKind::INT64,   // CSV_INT8
            NumpyColumnKind::INT64,   // scalar_uint8, reserved by classify_scalar
            NumpyColumnKind::INT64,   // CSV_INT16
            NumpyColumnKind::INT64,   // scalar_uint16, reserved by classify_scalar
            NumpyColumnKind::INT64,   // CSV_INT32
            NumpyColumnKind::INT64,   // scalar_uint32, reserved by classify_scalar
            NumpyColumnKind::INT64,   // CSV_INT64
            NumpyColumnKind::STRING,  // scalar_uint64, outside CSVField integral range
            NumpyColumnKind::STRING,  // CSV_BIGINT
            NumpyColumnKind::FLOAT64, // CSV_DOUBLE
            NumpyColumnKind::STRING,  // scalar_bigfloat, reserved by classify_scalar
            NumpyColumnKind::STRING   // CSV_TIMESTAMP
        };

        static_assert(static_cast<int>(DataType::CSV_NULL) == 0, "DataType lookup table assumes classify_scalar ids");
        static_assert(static_cast<int>(DataType::CSV_TIMESTAMP) == 14, "DataType lookup table must cover all builtin ids");

        const int index = static_cast<int>(type);
        if (index < 0 || index >= static_cast<int>(sizeof(type_to_kind) / sizeof(type_to_kind[0]))) {
            return NumpyColumnKind::STRING;
        }
        return type_to_kind[index];
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

    void start_typed_column(NumpyColumnKind observed, const csv::internals::CSVFieldScalar& scalar) {
        this->plan.kind = observed;
        if (this->plan.nullable || observed == NumpyColumnKind::FLOAT64) {
            this->floats.reset(new std::vector<double>());
            this->floats->assign(this->seen, (std::numeric_limits<double>::quiet_NaN)());
            this->append_float_value(scalar, observed);
            return;
        }

        if (observed == NumpyColumnKind::INT64) {
            this->ints.reset(new std::vector<std::int64_t>());
            this->ints->push_back(scalar.integer);
        }
        else {
            this->bools.reset(new std::vector<std::uint8_t>());
            this->bools->push_back(scalar.boolean ? 1 : 0);
        }
        ++this->seen;
    }

    void append_current_kind(const csv::internals::CSVFieldScalar& scalar, NumpyColumnKind observed) {
        if (this->storage_kind() == NumpyColumnKind::FLOAT64) {
            this->append_float_value(scalar, observed);
            return;
        }

        if (observed == NumpyColumnKind::INT64) {
            if (!this->ints) {
                this->ints.reset(new std::vector<std::int64_t>());
            }
            this->ints->push_back(scalar.integer);
        }
        else {
            if (!this->bools) {
                this->bools.reset(new std::vector<std::uint8_t>());
            }
            this->bools->push_back(scalar.boolean ? 1 : 0);
        }
        ++this->seen;
    }

    void append_float_value(const csv::internals::CSVFieldScalar& scalar, NumpyColumnKind observed) {
        if (!this->floats) {
            this->floats.reset(new std::vector<double>());
        }

        if (observed == NumpyColumnKind::BOOL) {
            this->floats->push_back(scalar.boolean ? 1.0 : 0.0);
        }
        else if (is_integral_type(scalar.type)) {
            this->floats->push_back(static_cast<double>(scalar.integer));
        }
        else {
            this->floats->push_back(static_cast<double>(scalar.floating));
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
            buffer.append_casted(column.get_sv(row_index));
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

inline bool is_all_string_plan(const std::vector<NumpyColumnPlan>& plan) noexcept {
    for (const auto& column : plan) {
        if (column.kind != NumpyColumnKind::STRING) {
            return false;
        }
    }
    return true;
}

inline bool is_numeric_numpy_kind(NumpyColumnKind kind) noexcept {
    return kind == NumpyColumnKind::INT64 || kind == NumpyColumnKind::FLOAT64;
}

inline bool should_eager_numpy_classification(
    bool cast,
    const std::vector<NumpyColumnPlan>& plan,
    size_t source_column_count
) noexcept {
    return cast && source_column_count != 0 && plan.size() >= source_column_count;
}

inline void promote_numpy_column_plan(NumpyColumnPlan& target, const NumpyColumnPlan& observed) {
    if (observed.kind == NumpyColumnKind::UNKNOWN) {
        target.nullable = target.nullable || observed.nullable;
        return;
    }

    if (observed.kind == NumpyColumnKind::STRING) {
        target.kind = NumpyColumnKind::STRING;
        target.nullable = false;
        return;
    }

    if (target.kind == NumpyColumnKind::UNKNOWN) {
        target.kind = observed.kind;
        target.nullable = target.nullable || observed.nullable;
        return;
    }

    if (target.kind == NumpyColumnKind::STRING) {
        target.nullable = false;
        return;
    }

    if (target.kind == observed.kind) {
        target.nullable = target.nullable || observed.nullable;
        return;
    }

    if (is_numeric_numpy_kind(target.kind) && is_numeric_numpy_kind(observed.kind)) {
        target.kind = NumpyColumnKind::FLOAT64;
        target.nullable = target.nullable || observed.nullable;
        return;
    }

    target.kind = NumpyColumnKind::STRING;
    target.nullable = false;
}

inline std::vector<NumpyColumnBuffer> make_numpy_buffers(const std::vector<NumpyColumnPlan>& plan) {
    std::vector<NumpyColumnBuffer> buffers;
    buffers.reserve(plan.size());
    for (const auto& column : plan) {
        buffers.emplace_back(column);
    }
    return buffers;
}

inline nb::dict numpy_buffers_to_dict(std::vector<NumpyColumnBuffer>& buffers) {
    nb::dict out;
    for (auto& buffer : buffers) {
        out[nb::str(buffer.plan.name.data(), buffer.plan.name.size())] = buffer.into_python();
    }
    return out;
}

inline size_t included_row_count(const DataFrame<>& frame, const std::vector<std::uint8_t>& excluded_rows) {
    size_t included = 0;
    for (size_t row_index = 0; row_index < frame.size(); ++row_index) {
        if (!(row_index < excluded_rows.size() && excluded_rows[row_index] != 0)) {
            ++included;
        }
    }
    return included;
}

inline nb::dict empty_numpy_batch(const std::vector<NumpyColumnPlan>& plan) {
    std::vector<NumpyColumnBuffer> buffers = make_numpy_buffers(plan);
    finalize_numpy_buffers(buffers);
    return numpy_buffers_to_dict(buffers);
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
            buffer.append_casted(column.get_sv(row_index));
        }
    }
}

inline void append_numpy_column_fixed_filtered(
    DataFrame<>::column_type column,
    NumpyColumnBuffer& buffer,
    const std::vector<std::uint8_t>& deleted_rows
) {
    for (size_t row_index = 0; row_index < column.size(); ++row_index) {
        if (!row_is_deleted(deleted_rows, row_index)) {
            buffer.append_fixed_casted(column.get_sv(row_index));
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

inline nb::dict materialize_numpy_batch(
    const DataFrame<>& batch,
    const std::vector<NumpyColumnPlan>& plan,
    const std::vector<size_t>& selected_indices,
    const std::vector<std::uint8_t>& excluded_rows,
    bool cast,
    bool fixed_schema,
    DataFrameExecutor& executor
) {
    std::vector<NumpyColumnBuffer> buffers = make_numpy_buffers(plan);
    if (cast) {
        batch.column_parallel_apply(
            executor,
            selected_indices,
            buffers,
            [&excluded_rows, fixed_schema](DataFrame<>::column_type column, NumpyColumnBuffer& buffer) {
                if (fixed_schema) {
                    append_numpy_column_fixed_filtered(column, buffer, excluded_rows);
                }
                else {
                    append_numpy_column_filtered(column, buffer, excluded_rows);
                }
            }
        );
        finalize_numpy_buffers(buffers);
    }
    else {
        batch.column_parallel_apply(
            executor,
            selected_indices,
            buffers,
            [&excluded_rows](DataFrame<>::column_type column, NumpyColumnBuffer& buffer) {
                append_numpy_strings_only_filtered(column, buffer, excluded_rows);
            }
        );
    }

    return numpy_buffers_to_dict(buffers);
}

inline void infer_numpy_batch_schema(
    const DataFrame<>& batch,
    std::vector<NumpyColumnPlan>& plan,
    const std::vector<size_t>& selected_indices,
    DataFrameExecutor& executor,
    const std::vector<std::uint8_t>& excluded_rows
) {
    if (is_all_string_plan(plan)) {
        return;
    }

    if (included_row_count(batch, excluded_rows) == 0) {
        return;
    }

    std::vector<NumpyColumnPlan> chunk_plan;
    chunk_plan.reserve(plan.size());
    for (const auto& column : plan) {
        NumpyColumnPlan observed;
        observed.name = column.name;
        observed.index = column.index;
        chunk_plan.push_back(std::move(observed));
    }

    std::vector<NumpyColumnBuffer> buffers = make_numpy_buffers(chunk_plan);
    batch.column_parallel_apply(
        executor,
        selected_indices,
        buffers,
        [&excluded_rows](DataFrame<>::column_type column, NumpyColumnBuffer& buffer) {
            append_numpy_column_filtered(column, buffer, excluded_rows);
        }
    );

    for (size_t index = 0; index < plan.size(); ++index) {
        promote_numpy_column_plan(plan[index], buffers[index].plan);
    }
}

inline void infer_numpy_batch_schema(
    CSVReader& reader,
    std::vector<NumpyColumnPlan>& plan,
    const std::vector<size_t>& selected_indices,
    const RowPredicate* predicate,
    DataFrameExecutor& executor,
    size_t batch_size
) {
    std::vector<CSVRow> rows;
    while (reader.read_chunk(rows, batch_size)) {
        DataFrame<> batch(std::move(rows));
        const std::vector<std::uint8_t> excluded_rows = excluded_rows_for_predicate(batch, {}, predicate);
        infer_numpy_batch_schema(batch, plan, selected_indices, executor, excluded_rows);
    }
}

class NumpyBatchReader {
public:
    NumpyBatchReader(
        std::string filename,
        nb::object columns,
        bool cast,
        nb::object predicate,
        size_t batch_size,
        std::string schema
    )
        : filename_(std::move(filename)),
          format_(CSVFormat::guess_csv()),
          cast_(cast),
          batch_size_(batch_size),
          schema_mode_(parse_numpy_batch_schema(std::move(schema))) {
        if (this->batch_size_ == 0) {
            throw std::invalid_argument("read_numpy_batches() batch_size must be greater than zero");
        }

        CSVReader schema_reader(this->filename_, this->format_);
        const std::vector<std::string> column_names = schema_reader.get_col_names();
        this->plan_ = make_numpy_column_plan(column_names, columns);
        this->selected_indices_ = selected_column_indices(this->plan_);
        if (should_eager_numpy_classification(this->cast_, this->plan_, column_names.size())) {
            this->format_.eager_field_classification();
            schema_reader = CSVReader(this->filename_, this->format_);
        }

        const RowPredicate* row_predicate = optional_row_predicate(predicate);
        if (row_predicate) {
            row_predicate->validate_columns(column_names);
            this->predicate_.reset(new RowPredicate(*row_predicate));
        }

        if (!this->cast_) {
            for (auto& column : this->plan_) {
                column.kind = NumpyColumnKind::STRING;
            }
        }
        else if (this->schema_mode_ == NumpyBatchSchemaMode::GLOBAL) {
            DataFrameExecutor schema_executor(numpy_worker_count(this->selected_indices_.size()));
            infer_numpy_batch_schema(
                schema_reader,
                this->plan_,
                this->selected_indices_,
                this->predicate_.get(),
                schema_executor,
                this->batch_size_
            );
        }

        this->reader_.reset(new CSVReader(this->filename_, this->format_));
        this->executor_.reset(new DataFrameExecutor(numpy_worker_count(this->selected_indices_.size())));

        if (this->cast_ && this->schema_mode_ == NumpyBatchSchemaMode::SAMPLE) {
            this->read_sample_schema_batch();
        }
    }

    NumpyBatchReader(const NumpyBatchReader&) = delete;
    NumpyBatchReader& operator=(const NumpyBatchReader&) = delete;
    NumpyBatchReader(NumpyBatchReader&&) noexcept = default;
    NumpyBatchReader& operator=(NumpyBatchReader&&) noexcept = default;

    NumpyBatchReader& iter() noexcept {
        return *this;
    }

    nb::dict next() {
        if (this->done_) {
            throw nb::stop_iteration();
        }

        std::vector<CSVRow> rows;
        if (this->has_pending_rows_) {
            rows = std::move(this->pending_rows_);
            this->has_pending_rows_ = false;
            nb::object batch = this->materialize_rows(std::move(rows));
            if (!batch.is_none()) {
                return nb::cast<nb::dict>(batch);
            }
        }

        while (this->reader_->read_chunk(rows, this->batch_size_)) {
            nb::object batch = this->materialize_rows(std::move(rows));
            if (!batch.is_none()) {
                return nb::cast<nb::dict>(batch);
            }
        }

        this->done_ = true;
        if (this->emitted_rows_ == 0 && !this->emitted_empty_batch_) {
            this->emitted_empty_batch_ = true;
            return empty_numpy_batch(this->plan_);
        }

        throw nb::stop_iteration();
    }

private:
    void read_sample_schema_batch() {
        if (!this->reader_->read_chunk(this->pending_rows_, this->batch_size_)) {
            return;
        }

        this->has_pending_rows_ = true;
        DataFrame<> batch(this->pending_rows_);
        const std::vector<std::uint8_t> excluded_rows =
            excluded_rows_for_predicate(batch, {}, this->predicate_.get());
        infer_numpy_batch_schema(batch, this->plan_, this->selected_indices_, *this->executor_, excluded_rows);
    }

    nb::object materialize_rows(std::vector<CSVRow> rows) {
        DataFrame<> batch(std::move(rows));
        const std::vector<std::uint8_t> excluded_rows =
            excluded_rows_for_predicate(batch, {}, this->predicate_.get());
        const size_t included = included_row_count(batch, excluded_rows);
        if (included == 0) {
            return nb::none();
        }

        std::vector<NumpyColumnPlan> batch_plan = this->plan_;
        const bool infer_per_batch = this->cast_ && this->schema_mode_ == NumpyBatchSchemaMode::BATCH;
        if (infer_per_batch) {
            DataFrameExecutor schema_executor(numpy_worker_count(this->selected_indices_.size()));
            infer_numpy_batch_schema(batch, batch_plan, this->selected_indices_, schema_executor, excluded_rows);
        }

        this->emitted_rows_ += included;
        return materialize_numpy_batch(
            batch,
            batch_plan,
            this->selected_indices_,
            excluded_rows,
            this->cast_,
            this->cast_ && !infer_per_batch,
            *this->executor_
        );
    }

    std::string filename_;
    CSVFormat format_;
    bool cast_ = true;
    size_t batch_size_ = NUMPY_CHUNK_ROWS;
    NumpyBatchSchemaMode schema_mode_ = NumpyBatchSchemaMode::SAMPLE;
    std::vector<NumpyColumnPlan> plan_;
    std::vector<size_t> selected_indices_;
    std::unique_ptr<RowPredicate> predicate_;
    std::unique_ptr<CSVReader> reader_;
    std::unique_ptr<DataFrameExecutor> executor_;
    std::vector<CSVRow> pending_rows_;
    size_t emitted_rows_ = 0;
    bool has_pending_rows_ = false;
    bool emitted_empty_batch_ = false;
    bool done_ = false;
};

nb::dict read_numpy(const std::string& filename, nb::object columns, bool cast, nb::object predicate) {
    CSVFormat format = CSVFormat::guess_csv();

    CSVReader header_reader(filename, format);
    const std::vector<std::string> column_names = header_reader.get_col_names();
    std::vector<NumpyColumnPlan> plan = make_numpy_column_plan(column_names, columns);
    const std::vector<size_t> selected_indices = selected_column_indices(plan);
    if (should_eager_numpy_classification(cast, plan, column_names.size())) {
        format.eager_field_classification();
        header_reader = CSVReader(filename, format);
    }

    DataFrameExecutor executor(numpy_worker_count(selected_indices.size()));
    const RowPredicate* row_predicate = optional_row_predicate(predicate);
    if (row_predicate) {
        row_predicate->validate_columns(column_names);
    }

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

void init_CSVNumpy(nb::module_& m) {
    nb::class_<NumpyBatchReader>(m, "_NumpyBatchReader")
    .def("__iter__", &NumpyBatchReader::iter, nb::rv_policy::reference_internal)
    .def("__next__", &NumpyBatchReader::next);

    m.def(
        "read_numpy_batches",
        [](std::string path, nb::object columns, nb::object predicate, bool cast, size_t batch_size, std::string schema) {
            return NumpyBatchReader(std::move(path), columns, cast, predicate, batch_size, std::move(schema));
        },
        "Parse a CSV file into streaming NumPy array batches keyed by column name.",
        nb::arg("path"),
        nb::arg("columns") = nb::none(),
        nb::arg("predicate") = nb::none(),
        nb::arg("cast") = true,
        nb::arg("batch_size") = NUMPY_CHUNK_ROWS,
        nb::arg("schema") = "sample"
    );
}
