/** @file
 *  @brief Stable storage for eager CSV field scalar classifications
 */

#pragma once

#include <algorithm>
#include <atomic>
#include <cassert>
#include <memory>
#include <utility>
#include <vector>

#include "../common.hpp"
#include "../data_type.hpp"
#include "constants.hpp"

namespace csv {
    namespace internals {
        namespace memory {
            /** Stable sidecar storage for parser-time CSVFieldScalar values.
             *
             *  This intentionally mirrors RawCSVFieldList allocation semantics:
             *  scalar values are stored in fixed-size blocks whose addresses stay
             *  stable while the parser appends later fields from the same chunk.
             *  Cross-thread visibility is provided by the row queue mutex plus
             *  atomic size/block publication, matching RawCSVFieldList.
             */
            class CSVFieldScalarList {
            public:
                CSVFieldScalarList(size_t single_buffer_capacity = (size_t)(internals::PAGE_SIZE / sizeof(CSVFieldScalar))) :
                    block_capacity_(single_buffer_capacity == 0 ? 1 : single_buffer_capacity) {
                    const size_t max_fields = internals::CSV_CHUNK_SIZE_DEFAULT + 1;
                    const size_t block_count = (max_fields + this->block_capacity_ - 1) / this->block_capacity_;
                    this->blocks_.reserve(block_count);
                    this->blocks_.resize(block_count);
                }

                CSVFieldScalarList(const CSVFieldScalarList&) = delete;

                CSVFieldScalarList(CSVFieldScalarList&& other) noexcept
                    : block_capacity_(other.block_capacity_),
                      blocks_(std::move(other.blocks_)) {
                    this->size_.store(other.size_.load(std::memory_order_acquire), std::memory_order_release);
                    this->block_count_.store(other.block_count_.load(std::memory_order_acquire), std::memory_order_release);
                    other.size_.store(0, std::memory_order_release);
                    other.block_count_.store(0, std::memory_order_release);
                }

                void emplace_back(const CSVFieldScalar& scalar) {
                    const size_t offset = this->size_.load(std::memory_order_acquire);
                    const size_t page_no = offset / this->block_capacity_;
                    const size_t buffer_idx = offset % this->block_capacity_;

                    if (page_no >= this->block_count_.load(std::memory_order_acquire)) {
                        this->allocate_block(page_no);
                    }

                    this->blocks_[page_no].get()[buffer_idx] = scalar;
                    this->size_.store(offset + 1, std::memory_order_release);
                }

                size_t size() const noexcept {
                    return this->size_.load(std::memory_order_acquire);
                }

                bool empty() const noexcept {
                    return this->size() == 0;
                }

                void reserve_for_source_size(size_t source_size) {
                    const size_t max_fields = source_size + 1;
                    const size_t block_count = (max_fields + this->block_capacity_ - 1) / this->block_capacity_;
                    if (block_count > this->blocks_.size()) {
                        this->blocks_.resize(block_count);
                    }
                }

                inline const CSVFieldScalar& operator[](size_t n) const {
                    assert(n < this->size_.load(std::memory_order_acquire));
                    const size_t page_no = n / this->block_capacity_;
                    const size_t buffer_idx = n % this->block_capacity_;
                    assert(page_no < this->block_count_.load(std::memory_order_acquire));
                    return this->blocks_[page_no].get()[buffer_idx];
                }

            private:
                size_t block_capacity_;
                std::vector<std::unique_ptr<CSVFieldScalar[]>> blocks_;
                std::atomic<size_t> size_{ 0 };
                std::atomic<size_t> block_count_{ 0 };

                void allocate_block(size_t page_no) {
                    if (page_no >= this->blocks_.size()) {
                        this->blocks_.resize((std::max)(page_no + 1, this->blocks_.size() * 2));
                    }

                    if (!this->blocks_[page_no]) {
                        this->blocks_[page_no] = std::unique_ptr<CSVFieldScalar[]>(new CSVFieldScalar[this->block_capacity_]);
                    }

                    this->block_count_.store(page_no + 1, std::memory_order_release);
                }
            };
        }
    }
}
