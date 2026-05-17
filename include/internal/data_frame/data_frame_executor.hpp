#pragma once

#include <thread>
#include <utility>

#include "../common.hpp"
#include "../parallel/indexed_task_pool.hpp"

namespace csv {
    class DataFrameExecutor {
    public:
        explicit DataFrameExecutor(size_t worker_count = default_worker_count())
            : task_pool_(worker_count) {}

        DataFrameExecutor(const DataFrameExecutor&) = delete;
        DataFrameExecutor& operator=(const DataFrameExecutor&) = delete;

        ~DataFrameExecutor() {}

        size_t worker_count() const noexcept {
            return this->task_pool_.worker_count();
        }

        template<typename Fn>
        void parallel_for(size_t task_count, Fn&& fn) {
            if (task_count == 0) {
                return;
            }

#if CSV_ENABLE_THREADS
            const size_t workers = this->task_pool_.worker_count();
            if (workers <= 1 || task_count <= workers) {
                this->run_serial(task_count, std::forward<Fn>(fn));
                return;
            }

            this->task_pool_.parallel_for(task_count, [&fn](size_t, size_t task_index) {
                fn(task_index);
            });
#else
            this->run_serial(task_count, std::forward<Fn>(fn));
#endif
        }

    private:
        template<typename Fn>
        void run_serial(size_t task_count, Fn&& fn) {
            for (size_t i = 0; i < task_count; ++i) {
                fn(i);
            }
        }

        static size_t default_worker_count() {
#if CSV_ENABLE_THREADS
            const unsigned int hw = std::thread::hardware_concurrency();
            return hw > 0 ? static_cast<size_t>(hw) : 1;
#else
            return 0;
#endif
        }

        internals::parallel::IndexedTaskPool task_pool_;
    };
}
