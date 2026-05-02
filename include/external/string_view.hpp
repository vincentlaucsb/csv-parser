// Copyright 2017-2019 by Martin Moene
//
// Originally based on string-view lite, a C++17-like string_view for C++98 and later.
// For more information see https://github.com/martinmoene/string-view-lite
//
// Distributed under the Boost Software License, Version 1.0.
// (See accompanying file LICENSE.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//
// csv-parser local note:
// This vendored copy has been reduced to the char-only compatibility surface
// csv-parser uses in pre-C++17 builds.
//
// csv-parser is a byte-oriented parser. This shim intentionally exposes only
// nonstd::string_view, not generic basic_string_view or UTF-16/UTF-32 aliases.

#pragma once

#ifndef NONSTD_SV_LITE_H_INCLUDED
#define NONSTD_SV_LITE_H_INCLUDED

#include <algorithm>
#include <cstring>
#include <functional>
#include <ostream>
#include <stdexcept>
#include <string>

namespace nonstd {

class string_view {
public:
    typedef const char* const_iterator;
    typedef const_iterator iterator;
    typedef char value_type;
    typedef const char* pointer;
    typedef const char& reference;
    typedef const char& const_reference;
    typedef std::size_t size_type;
    typedef std::ptrdiff_t difference_type;

    enum : size_type { npos = static_cast<size_type>(-1) };

    constexpr string_view() noexcept : data_(""), size_(0) {}

    string_view(const char* str)
        : data_(str), size_(str ? std::char_traits<char>::length(str) : 0) {}

    constexpr string_view(const char* str, size_type len) noexcept
        : data_(str ? str : ""), size_(str ? len : 0) {}

    string_view(const std::string& str) noexcept
        : data_(str.data()), size_(str.size()) {}

    const_iterator begin() const noexcept { return data_; }
    const_iterator end() const noexcept { return data_ + size_; }
    const_iterator cbegin() const noexcept { return begin(); }
    const_iterator cend() const noexcept { return end(); }

    const char* data() const noexcept { return data_; }
    size_type size() const noexcept { return size_; }
    size_type length() const noexcept { return size_; }
    bool empty() const noexcept { return size_ == 0; }

    const_reference operator[](size_type pos) const noexcept { return data_[pos]; }

    const_reference at(size_type pos) const {
        if (pos >= size_) {
            throw std::out_of_range("nonstd::string_view::at()");
        }
        return data_[pos];
    }

    const_reference front() const noexcept { return data_[0]; }
    const_reference back() const noexcept { return data_[size_ - 1]; }

    void remove_prefix(size_type n) noexcept {
        data_ += n;
        size_ -= n;
    }

    void remove_suffix(size_type n) noexcept {
        size_ -= n;
    }

    string_view substr(size_type pos = 0, size_type count = npos) const {
        if (pos > size_) {
            throw std::out_of_range("nonstd::string_view::substr()");
        }
        return string_view(data_ + pos, (std::min)(count, size_ - pos));
    }

    int compare(string_view other) const noexcept {
        const size_type count = (std::min)(size_, other.size_);
        const int result = count == 0 ? 0 : std::char_traits<char>::compare(data_, other.data_, count);
        if (result != 0) {
            return result;
        }
        if (size_ == other.size_) {
            return 0;
        }
        return size_ < other.size_ ? -1 : 1;
    }

    size_type find(char ch, size_type pos = 0) const noexcept {
        if (pos >= size_) {
            return npos;
        }
        const void* found = std::memchr(data_ + pos, ch, size_ - pos);
        return found ? static_cast<const char*>(found) - data_ : npos;
    }

    size_type find(string_view needle, size_type pos = 0) const noexcept {
        if (needle.empty()) {
            return pos <= size_ ? pos : npos;
        }
        if (pos >= size_ || needle.size_ > size_ - pos) {
            return npos;
        }

        const const_iterator last = end() - static_cast<difference_type>(needle.size_) + 1;
        const_iterator it = std::search(begin() + static_cast<difference_type>(pos), last, needle.begin(), needle.end());
        return it == last ? npos : static_cast<size_type>(it - begin());
    }

    operator std::string() const {
        return std::string(data_, size_);
    }

private:
    const char* data_;
    size_type size_;
};

inline std::string to_string(string_view view) {
    return std::string(view.data(), view.size());
}

inline string_view to_string_view(const std::string& str) noexcept {
    return string_view(str);
}

inline bool operator==(string_view lhs, string_view rhs) noexcept { return lhs.compare(rhs) == 0; }
inline bool operator!=(string_view lhs, string_view rhs) noexcept { return !(lhs == rhs); }
inline bool operator<(string_view lhs, string_view rhs) noexcept { return lhs.compare(rhs) < 0; }
inline bool operator<=(string_view lhs, string_view rhs) noexcept { return lhs.compare(rhs) <= 0; }
inline bool operator>(string_view lhs, string_view rhs) noexcept { return lhs.compare(rhs) > 0; }
inline bool operator>=(string_view lhs, string_view rhs) noexcept { return lhs.compare(rhs) >= 0; }

inline std::ostream& operator<<(std::ostream& os, string_view view) {
    return os.write(view.data(), static_cast<std::streamsize>(view.size()));
}

} // namespace nonstd

namespace std {

template<>
struct hash<nonstd::string_view> {
    std::size_t operator()(nonstd::string_view view) const noexcept {
        return std::hash<std::string>()(std::string(view.data(), view.size()));
    }
};

} // namespace std

namespace nonstd {
inline namespace literals {
inline namespace string_view_literals {

inline string_view operator ""_sv(const char* str, std::size_t len) noexcept {
    return string_view(str, len);
}

} // namespace string_view_literals
} // namespace literals
} // namespace nonstd

#endif
