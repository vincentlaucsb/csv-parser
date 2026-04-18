/** @file
 *  @brief Non-seekable in-memory stream test utility
 *
 *  Simulates third-party stream sources (pipes, decompression wrappers, sockets)
 *  that do not support tellg()/seekg().
 */

#pragma once

#include <streambuf>
#include <string>
#include <istream>

/** Streambuf backed by a string but with seeking explicitly disabled. */
class NonSeekableStringBuf : public std::streambuf {
public:
    explicit NonSeekableStringBuf(std::string data) : storage_(std::move(data)) {
        if (!storage_.empty()) {
            char* begin = &storage_[0];
            this->setg(begin, begin, begin + static_cast<std::ptrdiff_t>(storage_.size()));
        }
    }

protected:
    pos_type seekoff(off_type, std::ios_base::seekdir,
        std::ios_base::openmode) override {
        return pos_type(off_type(-1));
    }

    pos_type seekpos(pos_type, std::ios_base::openmode) override {
        return pos_type(off_type(-1));
    }

private:
    std::string storage_;
};

/**
 * Non-seekable std::istream wrapper for regression tests.
 *
 * Supports normal forward reads, but tellg()/seekg() fail by design.
 */
class NonSeekableStream : public std::istream {
public:
    explicit NonSeekableStream(const std::string& data)
        : std::istream(nullptr), buffer_(data) {
        this->rdbuf(&buffer_);
    }

    NonSeekableStream(const NonSeekableStream&) = delete;
    NonSeekableStream& operator=(const NonSeekableStream&) = delete;

private:
    NonSeekableStringBuf buffer_;
};
