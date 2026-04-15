#pragma once

#include <algorithm>
#include <cstring>
#include <istream>

#include "common.hpp"

namespace csv {
    namespace internals {
        /**
         * streambuf adapter over csv::string_view with no data copy.
         *
         * The underlying memory must remain valid and immutable for the entire
         * lifetime of this stream object.
         */
        class StringViewStreamBuf : public std::streambuf {
        public:
            explicit StringViewStreamBuf(csv::string_view view) {
                char* begin = const_cast<char*>(view.data());
                char* end = begin + view.size();
                this->setg(begin, begin, end);
            }

        protected:
            int_type underflow() override {
                if (this->gptr() < this->egptr()) {
                    return traits_type::to_int_type(*this->gptr());
                }
                return traits_type::eof();
            }

            std::streamsize xsgetn(char* s, std::streamsize count) override {
                const std::streamsize avail = this->egptr() - this->gptr();
                const std::streamsize n = std::min(avail, count);
                if (n > 0) {
                    std::memcpy(s, this->gptr(), static_cast<size_t>(n));
                    this->gbump(static_cast<int>(n));
                }
                return n;
            }

            pos_type seekoff(off_type off, std::ios_base::seekdir dir,
                std::ios_base::openmode which = std::ios_base::in) override {
                if (!(which & std::ios_base::in)) {
                    return pos_type(off_type(-1));
                }

                const auto begin = this->eback();
                const auto curr = this->gptr();
                const auto end = this->egptr();

                off_type base = 0;
                if (dir == std::ios_base::beg) {
                    base = 0;
                }
                else if (dir == std::ios_base::cur) {
                    base = static_cast<off_type>(curr - begin);
                }
                else if (dir == std::ios_base::end) {
                    base = static_cast<off_type>(end - begin);
                }
                else {
                    return pos_type(off_type(-1));
                }

                const off_type next = base + off;
                const off_type size = static_cast<off_type>(end - begin);
                if (next < 0 || next > size) {
                    return pos_type(off_type(-1));
                }

                this->setg(begin, begin + next, end);
                return pos_type(next);
            }

            pos_type seekpos(pos_type pos,
                std::ios_base::openmode which = std::ios_base::in) override {
                return this->seekoff(off_type(pos), std::ios_base::beg, which);
            }
        };

        /**
         * Lightweight istream over csv::string_view with zero copy.
         *
         * WARNING: The caller is responsible for ensuring the backing memory
         * outlives this stream and any CSVReader parsing it.
         */
        class StringViewStream : public std::istream {
        public:
            explicit StringViewStream(csv::string_view view)
                : std::istream(nullptr), _buf(view) {
                this->rdbuf(&_buf);
            }

        private:
            StringViewStreamBuf _buf;
        };
    }
}
