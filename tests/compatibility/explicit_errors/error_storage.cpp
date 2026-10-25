#include "csv.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <new>
#include <vector>

namespace {
    std::atomic<bool> count_allocations(false);
    std::atomic<size_t> allocations(0);

    void* allocate(size_t size) {
        if (count_allocations.load(std::memory_order_relaxed))
            allocations.fetch_add(1, std::memory_order_relaxed);
        if (void* result = std::malloc(size ? size : 1)) return result;
        std::abort(); // Test-process allocation failure, outside the CSV error contract.
    }

    void start_counting() {
        allocations.store(0, std::memory_order_relaxed);
        count_allocations.store(true, std::memory_order_relaxed);
    }

    size_t stop_counting() {
        count_allocations.store(false, std::memory_order_relaxed);
        return allocations.load(std::memory_order_relaxed);
    }

    bool check(bool condition, const char* message) {
        if (!condition) std::fprintf(stderr, "CSVError storage: %s\n", message);
        return condition;
    }
}

// This standalone consumer has no test framework allocations to contaminate
// the counters. Its storage checks run before starting any parser workers.
void* operator new(size_t size) { return allocate(size); }
void* operator new[](size_t size) { return allocate(size); }
void operator delete(void* pointer) noexcept { std::free(pointer); }
void operator delete[](void* pointer) noexcept { std::free(pointer); }
#if defined(__cpp_sized_deallocation)
void operator delete(void* pointer, size_t) noexcept { std::free(pointer); }
void operator delete[](void* pointer, size_t) noexcept { std::free(pointer); }
#endif

int csv_error_storage_test() {
    using csv::CSVError;
    using csv::CSVErrorCode;
    static const char text[] = "A permanent diagnostic longer than a typical small-string buffer.";
    const std::error_code cause = std::make_error_code(std::errc::io_error);
    csv::CSVFormat format;
    CSVError setup_error;
    if (!format.try_delimiter({',', ';'}, setup_error)) return 36;

    start_counting();
    for (size_t i = 0; i < 100; ++i) {
        CSVError empty;
        empty.clear();
        CSVError copy(empty);
        CSVError moved(std::move(copy));
        empty = moved;
        copy = std::move(moved);
        empty.clear();
    }
    if (!check(stop_counting() == 0, "success must not allocate")) return 21;

    start_counting();
    auto borrowed = CSVError::from_static(CSVErrorCode::ReadFailed, text);
    borrowed.system = cause;
    CSVError copied(borrowed);
    CSVError moved(std::move(borrowed));
    CSVError assigned;
    assigned = copied;
    copied.clear();
    CSVError move_assigned;
    move_assigned = std::move(assigned);
    csv::internals::fail_static(copied, CSVErrorCode::ReadFailed, text);
    char delimiter = 0;
    const bool resolved = format.try_get_delim(delimiter, copied);
    bool utf8_bom = false;
    csv::internals::get_bom_skip(csv::string_view("\xff\xfe", 2), utf8_bom, copied);
    const size_t static_allocations = stop_counting();
    if (!check(static_allocations == 0 && !resolved && copied.code == CSVErrorCode::UnsupportedEncoding,
            "static errors, API failures, and their copies/moves must not allocate")
        || !check(moved.context.data() == text && move_assigned.context.data() == text,
            "static diagnostics must remain borrowed")
        || !check(moved.system == cause && move_assigned.system == cause, "copies/moves must preserve system causes")
        || !check(!borrowed && borrowed.context.empty() && !borrowed.system
            && !assigned && assigned.context.empty(), "moves must clear the source")) return 22;

    CSVError owned;
    {
        char runtime[] = "A runtime diagnostic longer than a typical small-string buffer.";
        const char* pointer = runtime;
        start_counting();
        csv::internals::fail(owned, CSVErrorCode::ReadFailed, pointer);
        const size_t dynamic_allocations = stop_counting();
        runtime[0] = 'X';
        if (!check(dynamic_allocations == 1, "runtime text must use one owned buffer")) return 23;
    }
    const std::string expected = "A runtime diagnostic longer than a typical small-string buffer.";
    if (!check(owned.message() == expected, "runtime text must outlive its source")) return 24;

    owned.system = cause;
    start_counting();
    CSVError deep_copy(owned);
    const size_t copy_allocations = stop_counting();
    if (!check(copy_allocations == 1 && deep_copy.context.data() != owned.context.data(),
        "owned copies must have independent buffers")) return 25;
    owned.set_context("replacement");
    owned.clear();
    if (!check(deep_copy.context == expected && deep_copy.system == cause,
        "copy must survive source replacement and clearing")) return 26;

    start_counting();
    const char* buffer = deep_copy.context.data();
    CSVError transferred(std::move(deep_copy));
    owned = std::move(transferred);
    const size_t move_allocations = stop_counting();
    if (!check(move_allocations == 0 && owned.context.data() == buffer,
            "owned moves must transfer the buffer without allocating")
        || !check(!deep_copy && deep_copy.context.empty() && !deep_copy.system
            && !transferred && transferred.context.empty(), "owned moves must clear the source")) return 27;

    CSVError assigned_owned;
    assigned_owned = owned;
    CSVError& self = assigned_owned;
    assigned_owned = self;
    assigned_owned = std::move(self);
    owned.clear();
    if (!check(assigned_owned.context == expected && assigned_owned.system == cause,
        "copy assignment and self-assignment must preserve ownership")) return 28;
    assigned_owned.set_context(assigned_owned.context.substr(2, 7));
    if (!check(assigned_owned.context == "runtime", "context replacement must accept its own subview")) return 29;

    std::vector<CSVError> errors;
    errors.push_back(assigned_owned);
    errors.push_back(CSVError(CSVErrorCode::ReadFailed, std::string(text)));
    assigned_owned.clear();
    if (!check(errors.front().context == "runtime" && errors.back().context == text,
        "owned diagnostics must survive vector relocation and temporary strings")) return 30;
    errors.front() = CSVError::from_static(CSVErrorCode::ReadFailed, text);
    errors.back().set_context(csv::string_view());
    if (!check(errors.front().context.data() == text && errors.back().message().empty(),
        "owned-to-static and owned-to-empty transitions must be safe")) return 31;

    auto bounded = CSVError::from_static(CSVErrorCode::ReadFailed, csv::string_view(text, 1));
    if (!check(bounded.message() == "A", "views need not be null-terminated")) return 32;
    const char bytes[] = {'a', '\0', 'b'};
    CSVError binary(CSVErrorCode::ReadFailed, csv::string_view(bytes, sizeof(bytes)));
    if (!check(binary.message() == std::string(bytes, sizeof(bytes)), "copies must preserve embedded nulls")) return 33;

#if CSV_EXCEPTIONS_ENABLED
    CSVError caught;
    try {
        throw std::runtime_error(text);
    } catch (const std::exception& failure) {
        csv::internals::fail(caught, CSVErrorCode::ReadFailed, failure.what());
    }
    if (!check(caught.context == text, "exception diagnostics must outlive the caught exception")) return 34;
    try {
        csv::internals::throw_error(bounded);
        return 35;
    } catch (const std::runtime_error& failure) {
        if (!check(std::string(failure.what()) == "A", "throwing adapters must respect the view length")) return 35;
    }
#endif
    return 0;
}
