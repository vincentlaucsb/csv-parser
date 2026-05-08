/** @file
 *  @brief Stable storage for RawCSVField metadata
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

#include "../common.hpp"
#include "constants.hpp"
#include "raw_csv_field.hpp"

namespace csv {
    namespace internals {
        namespace memory {
            /** A class used for efficiently storing RawCSVField objects and expanding as necessary
             *
             *  @par Implementation
             *  Stores fields in fixed-size page-aligned chunks via a vector of
             *  unique_ptr<RawCSVField[]> handles:
             *   - This design provides better cache locality when accessing sequential fields in a row
             *     as well as much lower memory allocation overhead.
             *   - Fixed-size blocks allow direct division/modulo lookup; this intentionally
             *     avoids the variable-block metadata needed by quote realization storage.
             * 
             *  @par Thread Safety
             *  Cross-thread visibility for field contents is provided by the records
             *  queue mutex in ThreadSafeDeque. The fixed arena publishes size/block
             *  state with atomics so readers can safely resolve already-emitted rows
             *  while the parser appends later fields from the same RawCSVData.
             */
            class RawCSVFieldList {
            public:
                /** Construct a RawCSVFieldList which allocates blocks of a certain size */
                RawCSVFieldList(size_t single_buffer_capacity = (size_t)(internals::PAGE_SIZE / sizeof(RawCSVField))) :
                    block_capacity_(single_buffer_capacity == 0 ? 1 : single_buffer_capacity) {
                    const size_t max_fields = internals::CSV_CHUNK_SIZE_DEFAULT + 1;
                    const size_t block_count = (max_fields + this->block_capacity_ - 1) / this->block_capacity_;
                    this->blocks_.reserve(block_count);
                    this->blocks_.resize(block_count);
                }

                // No copy constructor
                RawCSVFieldList(const RawCSVFieldList& other) = delete;

                // RawCSVFieldList may be moved with its backing blocks intact.
                RawCSVFieldList(RawCSVFieldList&& other) noexcept
                    : block_capacity_(other.block_capacity_),
                      blocks_(std::move(other.blocks_)) {
                    this->size_.store(other.size_.load(std::memory_order_acquire), std::memory_order_release);
                    this->block_count_.store(other.block_count_.load(std::memory_order_acquire), std::memory_order_release);
                    other.size_.store(0, std::memory_order_release);
                    other.block_count_.store(0, std::memory_order_release);
                }

                template <class... Args>
                void emplace_back(Args&&... args) {
                    const size_t offset = this->size_.load(std::memory_order_acquire);
                    const size_t page_no = offset / this->block_capacity_;
                    const size_t buffer_idx = offset % this->block_capacity_;

                    if (page_no >= this->block_count_.load(std::memory_order_acquire)) {
                        this->allocate_block(page_no);
                    }

                    this->blocks_[page_no].get()[buffer_idx] = RawCSVField(std::forward<Args>(args)...);
                    this->size_.store(offset + 1, std::memory_order_release);
                }

                size_t size() const noexcept {
                    return this->size_.load(std::memory_order_acquire);
                }

                void reserve_for_source_size(size_t source_size) {
                    const size_t max_fields = source_size + 1;
                    const size_t block_count = (max_fields + this->block_capacity_ - 1) / this->block_capacity_;
                    if (block_count > this->blocks_.size()) {
                        this->blocks_.resize(block_count);
                    }
                }

                /** Access a field by its index. This allows CSVRow objects to access fields
                 *  without knowing internal implementation details of RawCSVFieldList.
                 */
                inline RawCSVField& operator[](size_t n) const {
                    assert(n < this->size_.load(std::memory_order_acquire));
                    const size_t page_no = n / this->block_capacity_;
                    const size_t buffer_idx = n % this->block_capacity_;
                    assert(page_no < this->block_count_.load(std::memory_order_acquire));
                    return this->blocks_[page_no].get()[buffer_idx];
                }

            private:
                size_t block_capacity_;
                std::vector<std::unique_ptr<RawCSVField[]>> blocks_;
                std::atomic<size_t> size_{ 0 };
                std::atomic<size_t> block_count_{ 0 };

                void allocate_block(size_t page_no) {
                    if (page_no >= this->blocks_.size()) {
                        this->blocks_.resize((std::max)(page_no + 1, this->blocks_.size() * 2));
                    }

                    if (!this->blocks_[page_no]) {
                        this->blocks_[page_no] = std::unique_ptr<RawCSVField[]>(new RawCSVField[this->block_capacity_]);
                    }

                    this->block_count_.store(page_no + 1, std::memory_order_release);
                }
            };
        }
    }
}
