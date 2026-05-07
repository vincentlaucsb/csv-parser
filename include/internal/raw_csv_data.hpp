/** @file
 *  @brief Internal data structures for CSV parsing
 * 
 *  This file contains the low-level structures used by the parser to store
 *  CSV data before it's exposed through the public CSVRow/CSVField API.
 * 
 *  Data flow: Parser → RawCSVData → CSVRow → CSVField
 */

#pragma once
#include <algorithm>
#include <atomic>
#include <cassert>
#include <cstdint>
#include <limits>
#include <memory>
#include <utility>
#include <vector>

#include "common.hpp"
#include "col_names.hpp"

namespace csv {
    namespace internals {
        class JsonConverter;

        CONSTEXPR_VALUE_14 std::uint32_t INVALID_REALIZED_OFFSET = (std::numeric_limits<std::uint32_t>::max)();

        template<typename T>
        class RawCSVBlockArena {
        public:
            struct Allocation {
                std::uint32_t offset = 0;
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
                  blocks_(std::move(other.blocks_)),
                  size_(other.size_) {
                this->block_count_.store(other.block_count_.load(std::memory_order_acquire), std::memory_order_release);
                other.size_ = 0;
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
                this->size_ = other.size_;
                this->block_count_.store(other.block_count_.load(std::memory_order_acquire), std::memory_order_release);
                other.size_ = 0;
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
                    return Allocation{ this->checked_offset(this->size_), nullptr };
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
                this->size_ += count;
                return allocation;
            }

            T& operator[](size_t n) const {
                assert(n < this->size_);
                const Block& block = this->find_block(n);
                return block.values.get()[n - block.logical_start];
            }

            T& at_fixed(size_t n) const {
                assert(n < this->size_);
                const size_t page_no = n / this->default_block_capacity_;
                const size_t buffer_idx = n % this->default_block_capacity_;
                assert(page_no < this->block_count_.load(std::memory_order_acquire));
                return this->blocks_[page_no].values.get()[buffer_idx];
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

            size_t size() const noexcept {
                return this->size_;
            }

            void reserve_blocks(size_t count) {
                if (count > this->blocks_.size()) {
                    this->blocks_.resize(count);
                }
            }

        private:
            struct Block {
                Block() = default;
                Block(const Block&) = delete;
                Block& operator=(const Block&) = delete;
                Block(Block&& other) noexcept
                    : values(std::move(other.values)),
                      capacity(other.capacity),
                      logical_start(other.logical_start) {
                    used.store(other.used.load(std::memory_order_acquire), std::memory_order_release);
                }
                Block& operator=(Block&& other) noexcept {
                    if (this == &other) {
                        return *this;
                    }

                    values = std::move(other.values);
                    capacity = other.capacity;
                    used.store(other.used.load(std::memory_order_acquire), std::memory_order_release);
                    logical_start = other.logical_start;
                    return *this;
                }

                std::unique_ptr<T[]> values;
                size_t capacity = 0;
                std::atomic<size_t> used{ 0 };
                size_t logical_start = 0;
            };

            size_t default_block_capacity_;
            bool grow_blocks_;
            size_t next_block_capacity_ = 0;
            std::vector<Block> blocks_;
            size_t size_ = 0;
            std::atomic<size_t> block_count_{ 0 };

            std::uint32_t checked_offset(size_t value) const noexcept {
                assert(value <= INVALID_REALIZED_OFFSET);
                return static_cast<std::uint32_t>(value);
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

                Block& block = this->blocks_[block_count];
                block.values = std::unique_ptr<T[]>(new T[capacity]);
                block.capacity = capacity;
                block.used.store(0, std::memory_order_release);
                block.logical_start = this->size_;

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
                return this->blocks_[block_count - 1];
            }

            const Block& find_block(size_t offset) const {
                size_t low = 0;
                size_t high = this->block_count_.load(std::memory_order_acquire);
                while (low < high) {
                    const size_t mid = low + (high - low) / 2;
                    const Block& block = this->blocks_[mid];
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
                return this->blocks_[this->block_count_.load(std::memory_order_acquire) - 1];
            }
        };

        /** A barebones class used for describing CSV fields */
        struct RawCSVField {
            RawCSVField() = default;
            RawCSVField(
                size_t _start,
                size_t _length,
                bool _is_realized = false
            ) noexcept {
                assert(_start <= INVALID_REALIZED_OFFSET);
                assert(_length <= INVALID_REALIZED_OFFSET);
                start = static_cast<std::uint32_t>(_start);
                length = static_cast<std::uint32_t>(_length);
                is_realized = _is_realized;
            }

            /** Raw row-relative start, or quote-arena logical start when is_realized is true. */
            std::uint32_t start = 0;

            /** Field length in the selected backing storage. */
            std::uint32_t length = 0;

            /** True when start/length refer to RawCSVData::quote_arena instead of RawCSVData::data. */
            bool is_realized = false;

            CONSTEXPR bool has_realized_storage() const noexcept {
                return is_realized;
            }
        };

        /** A class used for efficiently storing RawCSVField objects and expanding as necessary
         *
         *  @par Implementation
         *  Stores fields in page-aligned chunks (~170 fields/chunk) via a vector of
         *  unique_ptr<RawCSVField[]>:
         *   - This design provides better cache locality when accessing sequential fields in a row
         *     as well as much lower memory allocation overhead.
         *   - The unique_ptr ensures STL container does not invalidate pointers to fields when resizing,
         *     which is critical to ensure memory safety and correctness of the parser.
         * 
         *  @par Thread Safety
         *  Cross-thread visibility for field contents is provided by the records
         *  queue mutex in ThreadSafeDeque. The reusable block arena publishes block
         *  metadata with atomics so readers can safely resolve already-emitted rows
         *  while the parser appends later fields from the same RawCSVData.
         */
        class RawCSVFieldList {
        public:
            /** Construct a RawCSVFieldList which allocates blocks of a certain size */
            RawCSVFieldList(size_t single_buffer_capacity = (size_t)(internals::PAGE_SIZE / sizeof(RawCSVField))) :
                arena_(single_buffer_capacity, false) {
                const size_t max_fields = internals::CSV_CHUNK_SIZE_DEFAULT + 1;
                const size_t block_capacity = (max_fields + single_buffer_capacity - 1) / single_buffer_capacity;
                this->arena_.reserve_blocks(block_capacity);
            }

            // No copy constructor
            RawCSVFieldList(const RawCSVFieldList& other) = delete;

            // CSVFieldArrays may be moved
            RawCSVFieldList(RawCSVFieldList&& other) noexcept = default;

            template <class... Args>
            void emplace_back(Args&&... args) {
                this->arena_.emplace_back(std::forward<Args>(args)...);
            }

            size_t size() const noexcept {
                return this->arena_.size();
            }

            /** Access a field by its index. This allows CSVRow objects to access fields
             *  without knowing internal implementation details of RawCSVFieldList.
             */
            inline RawCSVField& operator[](size_t n) const {
                return this->arena_.at_fixed(n);
            }

        private:
            RawCSVBlockArena<RawCSVField> arena_;
        };

        class RawCSVQuoteArena {
        public:
            RawCSVQuoteArena() : arena_(internals::PAGE_SIZE) {}

            std::uint32_t append(csv::string_view bytes) {
                if (bytes.empty()) {
                    return this->checked_offset(this->arena_.size());
                }

                auto allocation = this->arena_.allocate_contiguous(bytes.size());
                std::copy(bytes.begin(), bytes.end(), allocation.data);
                return allocation.offset;
            }

            RawCSVBlockArena<char>::Allocation allocate_contiguous(size_t length) {
                return this->arena_.allocate_contiguous(length);
            }

            csv::string_view view(size_t start, size_t length) const {
                return this->arena_.view(start, length);
            }

            void reserve_for_source_size(size_t source_size) {
                const size_t block_capacity = (source_size + internals::PAGE_SIZE - 1) / internals::PAGE_SIZE;
                this->arena_.reserve_blocks(block_capacity + 1);
            }

        private:
            RawCSVBlockArena<char> arena_;

            std::uint32_t checked_offset(size_t value) const noexcept {
                assert(value <= INVALID_REALIZED_OFFSET);
                return static_cast<std::uint32_t>(value);
            }
        };

        /** A class for storing raw CSV data and associated metadata
         * 
         *  This structure is the bridge between the parser thread and the main thread.
         *  Parser populates fields, data, and parse_flags; main thread reads via CSVRow.
         */
        struct RawCSVData {
            std::shared_ptr<void> _data = nullptr;
            csv::string_view data = "";

            internals::RawCSVFieldList fields;

            /** Parser-time sidecar bytes for fields whose quoted contents contained doubled quotes. */
            internals::RawCSVQuoteArena quote_arena;

            /** Cached JSON converter for rows sharing this parsed backing storage. */
            mutable internals::lazy_shared_ptr<JsonConverter> json_converter;

            internals::ColNamesPtr col_names = nullptr;
            internals::ParseFlagMap parse_flags;
            internals::WhitespaceMap ws_flags;

            /** True when at least one whitespace trim character is configured.
             *  Used by get_field_impl() to skip trim work in the common no-trim case.
             */
            bool has_ws_trimming = false;
        };

        using RawCSVDataPtr = std::shared_ptr<RawCSVData>;
    }
}
