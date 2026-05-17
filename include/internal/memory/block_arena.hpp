/** @file
 *  @brief Stable append-only arenas used by parser backing storage
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

#include "../common.hpp"

namespace csv {
    namespace internals {
        namespace memory {
            /** Variable-size block arena used for quote realization bytes.
             *
             *  Quote fields can be larger than the default page and blocks may grow,
             *  so this arena carries per-block metadata and resolves logical offsets
             *  by binary search.
             */
            template<typename T>
            class RawCSVBlockArena {
            public:
                struct Allocation {
                    Allocation() : offset(0), data(nullptr) {}

                    Allocation(CSVChunkIndex offset_, T* data_) : offset(offset_), data(data_) {}

                    CSVChunkIndex offset = 0;
                    T* data = nullptr;
                };

                explicit RawCSVBlockArena(size_t default_block_capacity, bool grow_blocks = true)
                    : default_block_capacity_(default_block_capacity == 0 ? 1 : default_block_capacity),
                      grow_blocks_(grow_blocks) {
                    this->blocks_.reserve(1);
                }

                RawCSVBlockArena(const RawCSVBlockArena&) = delete;
                RawCSVBlockArena& operator=(const RawCSVBlockArena&) = delete;

                RawCSVBlockArena(RawCSVBlockArena&& other) noexcept
                    : default_block_capacity_(other.default_block_capacity_),
                      grow_blocks_(other.grow_blocks_),
                      next_block_capacity_(other.next_block_capacity_),
                      blocks_(std::move(other.blocks_)) {
                    this->size_.store(other.size_.load(std::memory_order_acquire), std::memory_order_release);
                    this->block_count_.store(other.block_count_.load(std::memory_order_acquire), std::memory_order_release);
                    other.size_.store(0, std::memory_order_release);
                    other.block_count_.store(0, std::memory_order_release);
                }

                RawCSVBlockArena& operator=(RawCSVBlockArena&& other) noexcept {
                    if (this == &other) {
                        return *this;
                    }

                    this->default_block_capacity_ = other.default_block_capacity_;
                    this->grow_blocks_ = other.grow_blocks_;
                    this->next_block_capacity_ = other.next_block_capacity_;
                    this->blocks_ = std::move(other.blocks_);
                    this->size_.store(other.size_.load(std::memory_order_acquire), std::memory_order_release);
                    this->block_count_.store(other.block_count_.load(std::memory_order_acquire), std::memory_order_release);
                    other.size_.store(0, std::memory_order_release);
                    other.block_count_.store(0, std::memory_order_release);
                    return *this;
                }

                template<class... Args>
                T& emplace_back(Args&&... args) {
                    Allocation allocation = this->allocate_contiguous(1);
                    *allocation.data = T(std::forward<Args>(args)...);
                    return *allocation.data;
                }

                Allocation allocate_contiguous(size_t count) {
                    if (count == 0) {
                        return Allocation{ this->checked_offset(this->size_.load(std::memory_order_acquire)), nullptr };
                    }

                    if (this->block_count_.load(std::memory_order_acquire) == 0
                        || this->current_block().used.load(std::memory_order_acquire) + count > this->current_block().capacity) {
                        this->allocate_block(count);
                    }

                    Block& block = this->current_block();
                    const size_t block_used = block.used.load(std::memory_order_acquire);
                    Allocation allocation{
                        this->checked_offset(block.logical_start + block_used),
                        block.values.get() + block_used
                    };
                    block.used.store(block_used + count, std::memory_order_release);
                    this->size_.fetch_add(count, std::memory_order_release);
                    return allocation;
                }

                T& operator[](size_t n) const {
                    assert(n < this->size_.load(std::memory_order_acquire));
                    const Block& block = this->find_block(n);
                    return block.values.get()[n - block.logical_start];
                }

                size_t size() const noexcept {
                    return this->size_.load(std::memory_order_acquire);
                }

                csv::string_view view(size_t offset, size_t length) const {
                    if (length == 0) {
                        return csv::string_view();
                    }

                    const Block& block = this->find_block(offset);
                    assert(offset + length <= block.logical_start + block.used.load(std::memory_order_acquire));
                    const size_t block_offset = offset - block.logical_start;
                    return csv::string_view(block.values.get() + block_offset, length);
                }

                void reserve_blocks(size_t count) {
                    if (count > this->blocks_.size()) {
                        this->blocks_.resize(count);
                    }
                }

            private:
                struct Block {
                    std::unique_ptr<T[]> values;
                    size_t capacity = 0;
                    std::atomic<size_t> used{ 0 };
                    size_t logical_start = 0;
                };

                size_t default_block_capacity_;
                bool grow_blocks_;
                size_t next_block_capacity_ = 0;
                std::vector<std::unique_ptr<Block>> blocks_;
                std::atomic<size_t> size_{ 0 };
                std::atomic<size_t> block_count_{ 0 };

                CSVChunkIndex checked_offset(size_t value) const noexcept {
                    assert(value <= CSV_CHUNK_INDEX_MAX);
                    return static_cast<CSVChunkIndex>(value);
                }

                void allocate_block(size_t required_capacity) {
                    const size_t block_count = this->block_count_.load(std::memory_order_acquire);
                    if (block_count >= this->blocks_.size()) {
                        this->blocks_.resize((std::max)(size_t(1), this->blocks_.size() * 2));
                    }

                    const size_t baseline = this->next_block_capacity_ == 0
                        ? this->default_block_capacity_
                        : this->next_block_capacity_;
                    const size_t capacity = (std::max)(baseline, required_capacity);

                    if (!this->blocks_[block_count]) {
                        this->blocks_[block_count] = std::unique_ptr<Block>(new Block());
                    }

                    Block& block = *this->blocks_[block_count];
                    block.values = std::unique_ptr<T[]>(new T[capacity]);
                    block.capacity = capacity;
                    block.used.store(0, std::memory_order_release);
                    block.logical_start = this->size_.load(std::memory_order_acquire);

                    if (required_capacity > baseline) {
                        this->next_block_capacity_ = this->default_block_capacity_;
                    }
                    else if (this->grow_blocks_ && baseline < this->default_block_capacity_ * 4) {
                        this->next_block_capacity_ = baseline * 2;
                    }
                    else {
                        this->next_block_capacity_ = baseline;
                    }

                    this->block_count_.store(block_count + 1, std::memory_order_release);
                }

                Block& current_block() {
                    const size_t block_count = this->block_count_.load(std::memory_order_acquire);
                    return *this->blocks_[block_count - 1];
                }

                const Block& find_block(size_t offset) const {
                    size_t low = 0;
                    size_t high = this->block_count_.load(std::memory_order_acquire);
                    while (low < high) {
                        const size_t mid = low + (high - low) / 2;
                        const Block& block = *this->blocks_[mid];
                        if (offset < block.logical_start) {
                            high = mid;
                        }
                        else if (offset >= block.logical_start + block.used.load(std::memory_order_acquire)) {
                            low = mid + 1;
                        }
                        else {
                            return block;
                        }
                    }

                    assert(false && "RawCSVBlockArena offset out of range");
                    return *this->blocks_[this->block_count_.load(std::memory_order_acquire) - 1];
                }
            };
        }
    }
}
