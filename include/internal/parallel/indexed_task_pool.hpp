#pragma once

#include "../common.hpp"

#include <atomic>
#include <exception>
#include <functional>
#include <utility>

#if CSV_ENABLE_THREADS
#include <condition_variable>
#include <mutex>
#include <thread>
#include <vector>
#endif

namespace csv {
    namespace internals {
        namespace parallel {

        class IndexedTaskPool {
        public:
            explicit IndexedTaskPool(size_t worker_count)
#if CSV_ENABLE_THREADS
                : worker_count_(worker_count)
#endif
            {
                this->start_workers(worker_count);
            }

            IndexedTaskPool(const IndexedTaskPool&) = delete;
            IndexedTaskPool& operator=(const IndexedTaskPool&) = delete;

            ~IndexedTaskPool() {
                this->stop_workers();
            }

            size_t worker_count() const noexcept {
#if CSV_ENABLE_THREADS
                return this->worker_count_;
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
                if (this->workers_.empty()) {
                    this->run_serial(task_count, std::forward<Fn>(fn));
                    return;
                }

                std::exception_ptr captured_exception;
                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    this->current_task_ = std::forward<Fn>(fn);
                    this->task_exception_ = nullptr;
                    this->next_task_.store(0);
                    this->task_count_ = task_count;
                    this->active_workers_ = this->workers_.size();
                    ++this->generation_;
                }

                this->task_ready_.notify_all();

                {
                    std::unique_lock<std::mutex> lock(this->mutex_);
                    this->task_done_.wait(lock, [this]() {
                        return this->completed_generation_ == this->generation_;
                    });

                    captured_exception = this->task_exception_;
                    this->current_task_ = std::function<void(size_t, size_t)>();
                    this->task_exception_ = nullptr;
                }

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
                for (size_t task_index = 0; task_index < task_count; ++task_index) {
                    fn(0, task_index);
                }
            }

#if CSV_ENABLE_THREADS
            size_t worker_count_ = 0;

            void start_workers(size_t worker_count) {
                if (worker_count <= 1) {
                    return;
                }

                this->workers_.reserve(worker_count);
                for (size_t worker_index = 0; worker_index < worker_count; ++worker_index) {
                    this->workers_.push_back(std::thread(&IndexedTaskPool::worker_loop, this, worker_index));
                }
            }

            void stop_workers() {
                {
                    std::lock_guard<std::mutex> lock(this->mutex_);
                    this->stop_ = true;
                    ++this->generation_;
                }

                this->task_ready_.notify_all();
                for (size_t i = 0; i < this->workers_.size(); ++i) {
                    if (this->workers_[i].joinable()) {
                        this->workers_[i].join();
                    }
                }
            }

            void worker_loop(size_t worker_index) {
                size_t seen_generation = 0;

                for (;;) {
                    {
                        std::unique_lock<std::mutex> lock(this->mutex_);
                        this->task_ready_.wait(lock, [this, &seen_generation]() {
                            return this->stop_ || this->generation_ != seen_generation;
                        });

                        if (this->stop_) {
                            return;
                        }

                        seen_generation = this->generation_;
                    }

                    for (;;) {
                        const size_t task_index = this->next_task_.fetch_add(1);
                        if (task_index >= this->task_count_) {
                            break;
                        }

                        try {
                            this->current_task_(worker_index, task_index);
                        }
                        catch (...) {
                            std::lock_guard<std::mutex> lock(this->mutex_);
                            if (!this->task_exception_) {
                                this->task_exception_ = std::current_exception();
                                this->next_task_.store(this->task_count_);
                            }
                            break;
                        }
                    }

                    {
                        std::lock_guard<std::mutex> lock(this->mutex_);
                        if (--this->active_workers_ == 0) {
                            this->completed_generation_ = seen_generation;
                            this->task_done_.notify_one();
                        }
                    }
                }
            }

            std::vector<std::thread> workers_;
            std::mutex mutex_;
            std::condition_variable task_ready_;
            std::condition_variable task_done_;
            std::function<void(size_t, size_t)> current_task_;
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
    }
}
