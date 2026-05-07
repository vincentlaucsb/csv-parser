/** @file
 *  @brief Stable storage for RawCSVField metadata
 */

#pragma once

#include <utility>

#include "../common.hpp"
#include "block_arena.hpp"
#include "raw_csv_field.hpp"

namespace csv {
    namespace internals {
        namespace memory {
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
        }
    }
}
