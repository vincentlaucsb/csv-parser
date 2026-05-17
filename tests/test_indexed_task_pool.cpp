#include <catch2/catch_all.hpp>
#include "internal/parallel/indexed_task_pool.hpp"

#include <stdexcept>
#include <vector>

#if CSV_ENABLE_THREADS
#include <mutex>
#endif

using csv::internals::parallel::IndexedTaskPool;

TEST_CASE("IndexedTaskPool serial fallback visits indexed tasks", "[indexed_task_pool]") {
    IndexedTaskPool pool(1);
    std::vector<size_t> workers;
    std::vector<size_t> tasks;

    pool.parallel_for(5, [&workers, &tasks](size_t worker_index, size_t task_index) {
        workers.push_back(worker_index);
        tasks.push_back(task_index);
    });

#if CSV_ENABLE_THREADS
    REQUIRE(pool.worker_count() == 1);
#else
    REQUIRE(pool.worker_count() == 0);
#endif
    REQUIRE(workers == std::vector<size_t>{ 0, 0, 0, 0, 0 });
    REQUIRE(tasks == std::vector<size_t>{ 0, 1, 2, 3, 4 });
}

TEST_CASE("IndexedTaskPool skips zero-task generations", "[indexed_task_pool]") {
    IndexedTaskPool pool(1);
    size_t calls = 0;

    pool.parallel_for(0, [&calls](size_t, size_t) {
        ++calls;
    });

    REQUIRE(calls == 0);
}

TEST_CASE("IndexedTaskPool propagates task exceptions and remains reusable", "[indexed_task_pool]") {
    IndexedTaskPool pool(2);

    REQUIRE_THROWS_AS(
        pool.parallel_for(8, [](size_t, size_t task_index) {
            if (task_index == 3) {
                throw std::runtime_error("task failure");
            }
        }),
        std::runtime_error
    );

    std::vector<size_t> visits(4, 0);
    pool.parallel_for(visits.size(), [&visits](size_t, size_t task_index) {
        visits[task_index]++;
    });

    REQUIRE(visits == std::vector<size_t>{ 1, 1, 1, 1 });
}

#if CSV_ENABLE_THREADS
TEST_CASE("IndexedTaskPool distributes indexed work across repeated generations", "[indexed_task_pool][threads]") {
    IndexedTaskPool pool(3);

    for (size_t generation = 0; generation < 3; ++generation) {
        std::vector<size_t> visits(64, 0);
        std::vector<size_t> worker_visits(pool.worker_count(), 0);
        std::mutex mutex;

        pool.parallel_for(visits.size(), [&visits, &worker_visits, &mutex](size_t worker_index, size_t task_index) {
            std::lock_guard<std::mutex> lock(mutex);
            visits[task_index]++;
            worker_visits[worker_index]++;
        });

        for (size_t task_index = 0; task_index < visits.size(); ++task_index) {
            REQUIRE(visits[task_index] == 1);
        }

        size_t total_worker_visits = 0;
        for (size_t worker_index = 0; worker_index < worker_visits.size(); ++worker_index) {
            total_worker_visits += worker_visits[worker_index];
        }

        REQUIRE(total_worker_visits == visits.size());
    }
}
#endif
