#pragma once

#include <cstddef>
#include <iterator>

namespace csv {
    namespace internals {
        template<typename Owner, typename Proxy, typename Accessor>
        class indexed_proxy_iterator {
        public:
            using value_type = Proxy;
            using difference_type = std::ptrdiff_t;
            using pointer = const Proxy*;
            using reference = const Proxy&;
            using iterator_category = std::random_access_iterator_tag;

            indexed_proxy_iterator() = default;
            indexed_proxy_iterator(Owner* owner, size_t index, Accessor accessor = Accessor())
                : owner_(owner), index_(index), accessor_(accessor) {}

            reference operator*() const {
                cached_proxy_ = accessor_(owner_, index_);
                return cached_proxy_;
            }

            pointer operator->() const {
                operator*();
                return &cached_proxy_;
            }

            indexed_proxy_iterator& operator++() { ++index_; return *this; }
            indexed_proxy_iterator operator++(int) { auto tmp = *this; ++index_; return tmp; }
            indexed_proxy_iterator& operator--() { --index_; return *this; }
            indexed_proxy_iterator operator--(int) { auto tmp = *this; --index_; return tmp; }

            indexed_proxy_iterator operator+(difference_type n) const {
                return indexed_proxy_iterator(owner_, static_cast<size_t>(index_ + n), accessor_);
            }

            indexed_proxy_iterator operator-(difference_type n) const {
                return indexed_proxy_iterator(owner_, static_cast<size_t>(index_ - n), accessor_);
            }

            difference_type operator-(const indexed_proxy_iterator& other) const {
                return static_cast<difference_type>(index_) - static_cast<difference_type>(other.index_);
            }

            bool operator==(const indexed_proxy_iterator& other) const {
                return owner_ == other.owner_ && index_ == other.index_;
            }

            bool operator!=(const indexed_proxy_iterator& other) const {
                return !(*this == other);
            }

        private:
            Owner* owner_ = nullptr;
            size_t index_ = 0;
            Accessor accessor_;
            mutable Proxy cached_proxy_;
        };
    }
}
