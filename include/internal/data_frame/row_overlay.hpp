#pragma once

#include <atomic>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>

#include "../common.hpp"

namespace csv {
    struct RowOverlay {
        RowOverlay() = default;
        RowOverlay(const RowOverlay&) = delete;
        RowOverlay& operator=(const RowOverlay&) = delete;

        RowOverlay(RowOverlay&& other) noexcept : values(std::move(other.values)) {
            busy.clear(std::memory_order_release);
        }

        RowOverlay& operator=(RowOverlay&& other) noexcept {
            if (this != &other) {
                values = std::move(other.values);
                busy.clear(std::memory_order_release);
            }
            return *this;
        }

        bool try_get_copy(size_t col_index, std::string& out) const {
            row_overlay_lock_guard lock(this);
            auto it = values.find(col_index);
            if (it == values.end()) {
                return false;
            }

            out = it->second;
            return true;
        }

        /** Return a view into an edited cell without copying.
         *
         *  This exists for read-only batch scans such as schema/type inference,
         *  where the caller immediately consumes the value and no concurrent
         *  sparse-overlay edits are expected. General cell access should keep
         *  using try_get_copy() or DataFrameCell so callers do not accidentally
         *  retain a view across later mutation.
         */
        bool try_get_view(size_t col_index, csv::string_view& out) const {
            row_overlay_lock_guard lock(this);
            auto it = values.find(col_index);
            if (it == values.end()) {
                return false;
            }

            out = csv::string_view(it->second);
            return true;
        }

        void set(size_t col_index, std::string value) {
            row_overlay_lock_guard lock(this);
            values[col_index] = std::move(value);
        }

        bool empty() const {
            row_overlay_lock_guard lock(this);
            return values.empty();
        }

    private:
        struct row_overlay_lock_guard {
            explicit row_overlay_lock_guard(const RowOverlay* overlay)
                : busy(const_cast<std::atomic_flag&>(overlay->busy)) {
                while (busy.test_and_set(std::memory_order_acquire)) {}
            }

            ~row_overlay_lock_guard() {
                busy.clear(std::memory_order_release);
            }

            std::atomic_flag& busy;
        };

        mutable std::atomic_flag busy = ATOMIC_FLAG_INIT;
        std::unordered_map<size_t, std::string> values;
    };

    struct RowOverlaySlot {
        RowOverlaySlot() noexcept : ptr(nullptr) {}
        RowOverlaySlot(const RowOverlaySlot&) = delete;
        RowOverlaySlot& operator=(const RowOverlaySlot&) = delete;

        RowOverlaySlot(RowOverlaySlot&& other) noexcept
            : ptr(nullptr),
            owned(std::move(other.owned)) {
            RowOverlay* overlay = owned.get();
            ptr.store(overlay, std::memory_order_release);
            other.ptr.store(nullptr, std::memory_order_release);
        }

        RowOverlaySlot& operator=(RowOverlaySlot&& other) noexcept {
            if (this != &other) {
                owned = std::move(other.owned);
                RowOverlay* overlay = owned.get();
                ptr.store(overlay, std::memory_order_release);
                other.ptr.store(nullptr, std::memory_order_release);
            }

            return *this;
        }

        RowOverlay* get() noexcept {
            return ptr.load(std::memory_order_acquire);
        }

        const RowOverlay* get() const noexcept {
            return ptr.load(std::memory_order_acquire);
        }

        RowOverlay* ensure() {
            RowOverlay* overlay = this->get();
            if (!overlay) {
                owned.reset(new RowOverlay());
                overlay = owned.get();
                ptr.store(overlay, std::memory_order_release);
            }

            return overlay;
        }

    private:
        std::atomic<RowOverlay*> ptr;
        std::unique_ptr<RowOverlay> owned;
    };
}
