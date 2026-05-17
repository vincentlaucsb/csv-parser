#pragma once

#include <atomic>
#include <condition_variable>
#include <exception>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>
#include <vector>

#include "../common.hpp"

namespace csv {
    class DataFrameExecutor {
    public:
        explicit DataFrameExecutor(size_t worker_count = default_worker_count()) {
            this->start_workers(worker_count);
        }

        DataFrameExecutor(const DataFrameExecutor&) = delete;
        DataFrameExecutor& operator=(const DataFrameExecutor&) = delete;

        ~DataFrameExecutor() {
            this->stop_workers();
        }

        size_t worker_count() const noexcept {
#if CSV_ENABLE_THREADS
            return workers_.size();
#else
            return 0;
#endif
        }

        template<typename Fn>
        void parallel_for(size_t task_count, Fn&& fn) {
            if (task_count == 0) {
                return;
            }

#if CSV_ENABLE_THREADS
            if (workers_.empty() || task_count <= workers_.size()) {
                this->run_serial(task_count, std::forward<Fn>(fn));
                return;
            }

            std::exception_ptr captured_exception;
            {
                std::unique_lock<std::mutex> lock(mutex_);
                current_task_ = std::forward<Fn>(fn);
                task_exception_ = nullptr;
                next_task_.store(0);
                task_count_ = task_count;
                active_workers_ = workers_.size();
                ++generation_;
            }

            task_ready_.notify_all();

            std::unique_lock<std::mutex> lock(mutex_);
            task_done_.wait(lock, [this]() {
                return completed_generation_ == generation_;
            });

            captured_exception = task_exception_;
            current_task_ = std::function<void(size_t)>();
            task_exception_ = nullptr;

            if (captured_exception) {
                std::rethrow_exception(captured_exception);
            }
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

#if CSV_ENABLE_THREADS
        void start_workers(size_t worker_count) {
            workers_.reserve(worker_count);
            for (size_t i = 0; i < worker_count; ++i) {
                workers_.push_back(std::thread(&DataFrameExecutor::worker_loop, this));
            }
        }

        void stop_workers() {
            {
                std::lock_guard<std::mutex> lock(mutex_);
                stop_ = true;
            }

            task_ready_.notify_all();
            for (auto& worker : workers_) {
                if (worker.joinable())
                    worker.join();
            }
        }

        void worker_loop() {
            size_t seen_generation = 0;
            std::unique_lock<std::mutex> lock(mutex_);

            while (true) {
                task_ready_.wait(lock, [this, seen_generation]() {
                    return stop_ || generation_ != seen_generation;
                });

                if (stop_) return;

                const size_t local_generation = generation_;
                seen_generation = local_generation;
                lock.unlock();

                while (true) {
                    const size_t task_index = next_task_.fetch_add(1);
                    if (task_index >= task_count_)
                        break;

                    try {
                        current_task_(task_index);
                    }
                    catch (...) {
                        lock.lock();
                        if (!task_exception_) {
                            task_exception_ = std::current_exception();
                            next_task_.store(task_count_);
                        }
                        lock.unlock();
                        break;
                    }
                }

                lock.lock();
                if (--active_workers_ == 0) {
                    completed_generation_ = local_generation;
                    task_done_.notify_one();
                }
            }
        }

        std::vector<std::thread> workers_;
        std::mutex mutex_;
        std::condition_variable task_ready_;
        std::condition_variable task_done_;
        std::function<void(size_t)> current_task_;
        std::exception_ptr task_exception_ = nullptr;
        std::atomic<size_t> next_task_{0};
        size_t task_count_ = 0;
        size_t active_workers_ = 0;
        size_t generation_ = 0;
        size_t completed_generation_ = 0;
        bool stop_ = false;
#else
        void start_workers(size_t) {}
        void stop_workers() {}
#endif
    };
}
