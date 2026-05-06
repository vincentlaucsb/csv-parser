/** @file
 *  @brief Runtime read scheduling for CSVReader.
 */

#pragma once

#include <exception>
#include <functional>
#include <utility>

#include "common.hpp"

#if CSV_ENABLE_THREADS
#include <mutex>
#include <thread>
#endif

namespace csv {
    namespace internals {
        /** Synchronous read scheduler used by no-thread builds and runtime opt-out.
         *
         *  It mirrors the threaded scheduler's tiny surface so CSVReader can keep
         *  worker-launch and exception-transfer details out of its public facade
         *  logic.
         */
        class SynchronousCSVReadScheduler {
        public:
            SynchronousCSVReadScheduler() noexcept = default;

            explicit SynchronousCSVReadScheduler(bool enabled) noexcept {
                (void)enabled;
            }

            void set_threading_enabled(bool enabled) noexcept {
                (void)enabled;
            }

            void run(std::function<void()> task) {
                this->clear_exception();
                try {
                    task();
                }
                catch (...) {
                    this->exception_ = std::current_exception();
                }
            }

            void run(
                std::function<void()> task,
                const std::function<void()>& before_async_run
            ) {
                (void)before_async_run;
                this->run(std::move(task));
            }

            void join() noexcept {}

            bool wait_if_active(
                const std::function<bool()>& is_waitable,
                const std::function<void()>& wait
            ) {
                (void)is_waitable;
                (void)wait;
                return false;
            }

            void clear_exception() noexcept {
                this->exception_ = nullptr;
            }

            std::exception_ptr take_exception() noexcept {
                auto eptr = this->exception_;
                this->exception_ = nullptr;
                return eptr;
            }

            void adopt_exception(std::exception_ptr eptr) noexcept {
                this->exception_ = std::move(eptr);
            }

            void rethrow_exception_if_any() {
                if (auto eptr = this->take_exception()) {
                    std::rethrow_exception(eptr);
                }
            }

        private:
            std::exception_ptr exception_ = nullptr;
        };

#if CSV_ENABLE_THREADS
        /** Thread-backed scheduler selected when CSV_ENABLE_THREADS is available.
         *
         *  This concrete scheduler always launches a worker thread. Runtime
         *  selection between this and SynchronousCSVReadScheduler is handled by
         *  CSVReadScheduler below so the hot worker path has no "should I thread?"
         *  branch.
         */
        class ThreadedCSVReadScheduler {
        public:
            ThreadedCSVReadScheduler() noexcept = default;

            ThreadedCSVReadScheduler(const ThreadedCSVReadScheduler&) = delete;
            ThreadedCSVReadScheduler& operator=(const ThreadedCSVReadScheduler&) = delete;

            ~ThreadedCSVReadScheduler() {
                this->join();
            }

            void run(
                std::function<void()> task,
                const std::function<void()>& before_async_run = std::function<void()>()
            ) {
                this->join();
                this->clear_exception();

                if (before_async_run) {
                    before_async_run();
                }

                this->worker_ = std::thread([this, task]() mutable {
                    this->run_now(std::move(task));
                });
            }

            void join() noexcept {
                if (this->worker_.joinable()) {
                    this->worker_.join();
                }
            }

            bool wait_if_active(
                const std::function<bool()>& is_waitable,
                const std::function<void()>& wait
            ) {
                if (is_waitable()) {
                    wait();
                    return true;
                }

                return false;
            }

            void clear_exception() noexcept {
                std::lock_guard<std::mutex> lock(this->exception_lock_);
                this->exception_ = nullptr;
            }

            std::exception_ptr take_exception() noexcept {
                std::lock_guard<std::mutex> lock(this->exception_lock_);
                auto eptr = this->exception_;
                this->exception_ = nullptr;
                return eptr;
            }

            void adopt_exception(std::exception_ptr eptr) noexcept {
                std::lock_guard<std::mutex> lock(this->exception_lock_);
                this->exception_ = std::move(eptr);
            }

            void rethrow_exception_if_any() {
                if (auto eptr = this->take_exception()) {
                    std::rethrow_exception(eptr);
                }
            }

        private:
            std::thread worker_;
            std::exception_ptr exception_ = nullptr;
            std::mutex exception_lock_;

            void run_now(std::function<void()> task) noexcept {
                try {
                    task();
                }
                catch (...) {
                    this->adopt_exception(std::current_exception());
                }
            }
        };

        /** Runtime scheduler selector used by CSVReader.
         *
         *  Keeps runtime threading policy at the boundary without virtual
         *  dispatch. The selected concrete scheduler is stored as a void* and
         *  identified by comparing it to the owned threaded scheduler.
         */
        class CSVReadScheduler {
        public:
            explicit CSVReadScheduler(bool threading_enabled = true) noexcept {
                this->select(threading_enabled);
            }

            CSVReadScheduler(const CSVReadScheduler&) = delete;
            CSVReadScheduler& operator=(const CSVReadScheduler&) = delete;

            ~CSVReadScheduler() {
                this->join();
            }

            void set_threading_enabled(bool enabled) noexcept {
                this->join();
                auto eptr = this->take_exception();
                this->select(enabled);
                this->adopt_exception(std::move(eptr));
            }

            void run(
                std::function<void()> task,
                const std::function<void()>& before_async_run = std::function<void()>()
            ) {
                if (this->is_threaded()) {
                    this->threaded_.run(std::move(task), before_async_run);
                }
                else {
                    this->sync_.run(std::move(task));
                }
            }

            bool wait_if_active(
                const std::function<bool()>& is_waitable,
                const std::function<void()>& wait
            ) {
                return this->is_threaded()
                    && this->threaded_.wait_if_active(is_waitable, wait);
            }

            void join() noexcept {
                if (this->is_threaded()) {
                    this->threaded_.join();
                }
            }

            void clear_exception() noexcept {
                if (this->is_threaded()) {
                    this->threaded_.clear_exception();
                }
                else {
                    this->sync_.clear_exception();
                }
            }

            std::exception_ptr take_exception() noexcept {
                return this->is_threaded()
                    ? this->threaded_.take_exception()
                    : this->sync_.take_exception();
            }

            void adopt_exception(std::exception_ptr eptr) noexcept {
                if (this->is_threaded()) {
                    this->threaded_.adopt_exception(std::move(eptr));
                }
                else {
                    this->sync_.adopt_exception(std::move(eptr));
                }
            }

            void rethrow_exception_if_any() {
                if (auto eptr = this->take_exception()) {
                    std::rethrow_exception(eptr);
                }
            }

        private:
            SynchronousCSVReadScheduler sync_;
            ThreadedCSVReadScheduler threaded_;
            void* scheduler_ = &sync_;

            void select(bool threading_enabled) noexcept {
                if (threading_enabled) {
                    this->scheduler_ = &this->threaded_;
                }
                else {
                    this->scheduler_ = &this->sync_;
                }
            }

            bool is_threaded() const noexcept {
                return this->scheduler_ == &this->threaded_;
            }
        };
#else
        using CSVReadScheduler = SynchronousCSVReadScheduler;
#endif
    }
}
