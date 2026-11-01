#pragma once

#include <cstddef>
#include <iterator>
#include <type_traits>
#include <utility>

#include "common.hpp"
#include "csv_exceptions.hpp"

namespace csv {
namespace internals {
    // Keep the field itself alive through conversion: a DataFrameCell may
    // own an edited string, so a view into a temporary cell would dangle.
    template<typename Field>
    class CheckedFieldValue {
    public:
        template<typename Row>
        CheckedFieldValue(Row& row, size_t index, CSVError& error)
            : field_(row.try_field(index, error)), error_(&error) {}

        template<typename Consumer>
        void with_view(Consumer&& consumer) const {
            // A failed try_field() need not return a readable field. Consume
            // immediately so even an owning get_sv() result outlives the call.
            if (!*error_) std::forward<Consumer>(consumer)(field_.get_sv());
        }

    private:
        Field field_;
        const CSVError* error_;
    };

    /** Borrow a row and error state for synchronous, single-pass field access.
     *  A failed access ends iteration and preserves the first error. Consumers
     *  must check that error before accepting any values already consumed.
     */
    template<typename Row>
    class CheckedFieldRange {
        using field_type = typename std::decay<decltype(std::declval<Row&>()
            .try_field(size_t{0}, std::declval<CSVError&>()))>::type;

    public:
        using field_value = CheckedFieldValue<field_type>;

        class iterator {
        public:
            using iterator_category = std::input_iterator_tag;
            using value_type = field_value;
            using difference_type = std::ptrdiff_t;
            using reference = field_value;
            using pointer = void;

            iterator() = default;
            iterator(const CheckedFieldRange* owner, size_t index)
                : owner_(owner), index_(index) {}

            field_value operator*() const {
                return field_value(*owner_->row_, index_, *owner_->error_);
            }

            iterator& operator++() {
                if (!owner_->failed()) ++index_;
                return *this;
            }

            iterator operator++(int) {
                iterator previous = *this;
                ++*this;
                return previous;
            }

            bool operator==(const iterator& other) const {
                return owner_ == other.owner_
                    && (index_ == other.index_ || (owner_ && owner_->failed()));
            }

            bool operator!=(const iterator& other) const { return !(*this == other); }

        private:
            const CheckedFieldRange* owner_ = nullptr;
            size_t index_ = 0;
        };

        CheckedFieldRange(Row& row, CSVError& error)
            : row_(&row), error_(&error) {
            if (error) return;
            // Validate even zero-width proxies, before asking for their size.
            if (!record_valid(row, 0)) {
                fail_static(error, CSVErrorCode::InvalidProxy, "CSV row or column proxy is invalid.");
                return;
            }
            size_ = row.size();
        }

        iterator begin() const { return iterator(this, 0); }
        iterator end() const { return iterator(this, size_); }

    private:
        template<typename T>
        static auto record_valid(const T& row, int) -> decltype(row.is_valid(), bool()) {
            return row.is_valid();
        }

        template<typename T>
        static bool record_valid(const T&, long) { return true; }

        bool failed() const { return static_cast<bool>(*error_); }

        Row* row_;
        CSVError* error_;
        size_t size_ = 0;
    };

    template<typename Row>
    CheckedFieldRange<Row> checked_field_range(Row& row, CSVError& error) {
        return CheckedFieldRange<Row>(row, error);
    }
}
}
