#include "fastpycsv_bindings.hpp"

#include <array>
#include <cstring>
#include <fstream>
#include <istream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <streambuf>
#include <vector>

#include <zlib-ng.h>

namespace {
    constexpr std::uint32_t EOCD_SIGNATURE = 0x06054b50u;
    constexpr std::uint32_t ZIP64_EOCD_SIGNATURE = 0x06064b50u;
    constexpr std::uint32_t ZIP64_LOCATOR_SIGNATURE = 0x07064b50u;
    constexpr std::uint32_t CENTRAL_HEADER_SIGNATURE = 0x02014b50u;
    constexpr std::uint32_t LOCAL_HEADER_SIGNATURE = 0x04034b50u;
    constexpr std::uint16_t METHOD_STORED = 0;
    constexpr std::uint16_t METHOD_DEFLATED = 8;
    constexpr std::uint16_t FLAG_ENCRYPTED = 1u << 0;
    constexpr std::uint16_t ZIP64_EXTRA_ID = 0x0001u;
    constexpr std::uint32_t ZIP32_MAX = 0xffffffffu;
    constexpr std::uint16_t ZIP16_MAX = 0xffffu;

    struct ZipMemberInfo {
        std::string name;
        std::uint16_t flags = 0;
        std::uint16_t method = 0;
        std::uint64_t compressed_size = 0;
        std::uint64_t uncompressed_size = 0;
        std::uint64_t local_header_offset = 0;
    };

    struct CentralDirectoryInfo {
        std::uint64_t offset = 0;
        std::uint64_t size = 0;
        std::uint64_t entries = 0;
    };

    [[noreturn]] void zip_error(const std::string& message) {
        throw nb::value_error(message.c_str());
    }

    std::uint16_t read_u16(const unsigned char* data) noexcept {
        return static_cast<std::uint16_t>(data[0])
            | (static_cast<std::uint16_t>(data[1]) << 8);
    }

    std::uint32_t read_u32(const unsigned char* data) noexcept {
        return static_cast<std::uint32_t>(data[0])
            | (static_cast<std::uint32_t>(data[1]) << 8)
            | (static_cast<std::uint32_t>(data[2]) << 16)
            | (static_cast<std::uint32_t>(data[3]) << 24);
    }

    std::uint64_t read_u64(const unsigned char* data) noexcept {
        return static_cast<std::uint64_t>(read_u32(data))
            | (static_cast<std::uint64_t>(read_u32(data + 4)) << 32);
    }

    void read_exact(std::ifstream& file, char* data, std::uint64_t size, const char* message) {
        if (size > static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)())) {
            throw std::runtime_error("ZIP read request is too large for this platform");
        }
        file.read(data, static_cast<std::streamsize>(size));
        if (static_cast<std::uint64_t>(file.gcount()) != size) {
            zip_error(message);
        }
    }

    void seek_abs(std::ifstream& file, std::uint64_t offset, const char* message) {
        if (offset > static_cast<std::uint64_t>((std::numeric_limits<std::streamoff>::max)())) {
            throw std::runtime_error("ZIP offset is too large for this platform");
        }
        file.clear();
        file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file) {
            zip_error(message);
        }
    }

    std::uint64_t file_size(std::ifstream& file) {
        file.clear();
        file.seekg(0, std::ios::end);
        const std::streamoff end = file.tellg();
        if (end < 0) {
            zip_error("failed to inspect ZIP archive size");
        }
        return static_cast<std::uint64_t>(end);
    }

    CentralDirectoryInfo read_zip64_eocd(
        std::ifstream& file,
        std::uint64_t locator_offset,
        std::uint64_t archive_size
    ) {
        std::array<unsigned char, 20> locator {};
        seek_abs(file, locator_offset, "failed to seek to ZIP64 locator");
        read_exact(file, reinterpret_cast<char*>(locator.data()), locator.size(), "truncated ZIP64 locator");
        if (read_u32(locator.data()) != ZIP64_LOCATOR_SIGNATURE) {
            zip_error("ZIP64 end-of-central-directory locator not found");
        }
        if (read_u32(locator.data() + 16) != 1) {
            zip_error("multi-disk ZIP archives are not supported");
        }

        const std::uint64_t eocd_offset = read_u64(locator.data() + 8);
        if (eocd_offset + 56 > archive_size) {
            zip_error("invalid ZIP64 end-of-central-directory offset");
        }

        std::array<unsigned char, 56> eocd {};
        seek_abs(file, eocd_offset, "failed to seek to ZIP64 end-of-central-directory");
        read_exact(file, reinterpret_cast<char*>(eocd.data()), eocd.size(), "truncated ZIP64 end-of-central-directory");
        if (read_u32(eocd.data()) != ZIP64_EOCD_SIGNATURE) {
            zip_error("ZIP64 end-of-central-directory not found");
        }
        if (read_u32(eocd.data() + 16) != 0 || read_u32(eocd.data() + 20) != 0) {
            zip_error("multi-disk ZIP archives are not supported");
        }

        CentralDirectoryInfo out;
        out.entries = read_u64(eocd.data() + 32);
        out.size = read_u64(eocd.data() + 40);
        out.offset = read_u64(eocd.data() + 48);
        if (out.offset + out.size > archive_size) {
            zip_error("invalid ZIP64 central directory bounds");
        }
        return out;
    }

    CentralDirectoryInfo find_central_directory(std::ifstream& file) {
        const std::uint64_t size = file_size(file);
        if (size < 22) {
            zip_error("not a valid ZIP archive");
        }
        const std::uint64_t search_size = (std::min<std::uint64_t>)(size, 22 + 0xffff);
        std::vector<unsigned char> tail(static_cast<size_t>(search_size));
        seek_abs(file, size - search_size, "failed to seek to ZIP end-of-central-directory");
        read_exact(file, reinterpret_cast<char*>(tail.data()), tail.size(), "failed to read ZIP end-of-central-directory");

        for (size_t pos = tail.size() - 22; pos != static_cast<size_t>(-1); --pos) {
            if (read_u32(tail.data() + pos) != EOCD_SIGNATURE) {
                continue;
            }

            const unsigned char* eocd = tail.data() + pos;
            const std::uint16_t disk = read_u16(eocd + 4);
            const std::uint16_t central_disk = read_u16(eocd + 6);
            if (disk != 0 || central_disk != 0) {
                zip_error("multi-disk ZIP archives are not supported");
            }

            const std::uint16_t entries_on_disk = read_u16(eocd + 8);
            const std::uint16_t entries_total = read_u16(eocd + 10);
            const std::uint32_t central_size = read_u32(eocd + 12);
            const std::uint32_t central_offset = read_u32(eocd + 16);
            const std::uint16_t comment_size = read_u16(eocd + 20);
            if (pos + 22 + comment_size != tail.size()) {
                continue;
            }

            const bool needs_zip64 =
                entries_on_disk == ZIP16_MAX ||
                entries_total == ZIP16_MAX ||
                central_size == ZIP32_MAX ||
                central_offset == ZIP32_MAX;
            if (needs_zip64) {
                const std::uint64_t eocd_absolute = size - search_size + pos;
                if (eocd_absolute < 20) {
                    zip_error("ZIP64 locator is missing");
                }
                return read_zip64_eocd(file, eocd_absolute - 20, size);
            }

            CentralDirectoryInfo out;
            out.entries = entries_total;
            out.size = central_size;
            out.offset = central_offset;
            if (out.offset + out.size > size) {
                zip_error("invalid ZIP central directory bounds");
            }
            return out;
        }

        zip_error("not a valid ZIP archive");
    }

    std::uint64_t read_zip64_value(
        const unsigned char*& cursor,
        const unsigned char* end,
        const char* message
    ) {
        if (end - cursor < 8) {
            zip_error(message);
        }
        const std::uint64_t value = read_u64(cursor);
        cursor += 8;
        return value;
    }

    void apply_zip64_extra(
        ZipMemberInfo& info,
        const unsigned char* extra,
        size_t extra_size,
        std::uint32_t compressed32,
        std::uint32_t uncompressed32,
        std::uint32_t offset32
    ) {
        const unsigned char* cursor = extra;
        const unsigned char* end = extra + extra_size;
        while (end - cursor >= 4) {
            const std::uint16_t id = read_u16(cursor);
            const std::uint16_t size = read_u16(cursor + 2);
            cursor += 4;
            if (end - cursor < size) {
                zip_error("truncated ZIP extra field");
            }

            if (id == ZIP64_EXTRA_ID) {
                const unsigned char* zip64 = cursor;
                const unsigned char* zip64_end = cursor + size;
                if (uncompressed32 == ZIP32_MAX) {
                    info.uncompressed_size = read_zip64_value(zip64, zip64_end, "truncated ZIP64 uncompressed size");
                }
                if (compressed32 == ZIP32_MAX) {
                    info.compressed_size = read_zip64_value(zip64, zip64_end, "truncated ZIP64 compressed size");
                }
                if (offset32 == ZIP32_MAX) {
                    info.local_header_offset = read_zip64_value(zip64, zip64_end, "truncated ZIP64 local header offset");
                }
                return;
            }

            cursor += size;
        }
    }

    std::vector<ZipMemberInfo> read_members(const std::string& path) {
        std::ifstream file(path.c_str(), std::ios::binary);
        if (!file) {
            zip_error("failed to open ZIP archive: " + path);
        }

        const CentralDirectoryInfo central = find_central_directory(file);
        std::vector<ZipMemberInfo> members;
        members.reserve(static_cast<size_t>((std::min<std::uint64_t>)(central.entries, 1024 * 1024)));

        seek_abs(file, central.offset, "failed to seek to ZIP central directory");
        std::uint64_t consumed = 0;
        for (std::uint64_t index = 0; index < central.entries; ++index) {
            std::array<unsigned char, 46> header {};
            read_exact(file, reinterpret_cast<char*>(header.data()), header.size(), "truncated ZIP central directory");
            consumed += header.size();
            if (read_u32(header.data()) != CENTRAL_HEADER_SIGNATURE) {
                zip_error("invalid ZIP central directory entry");
            }

            const std::uint16_t name_size = read_u16(header.data() + 28);
            const std::uint16_t extra_size = read_u16(header.data() + 30);
            const std::uint16_t comment_size = read_u16(header.data() + 32);
            const std::uint32_t compressed32 = read_u32(header.data() + 20);
            const std::uint32_t uncompressed32 = read_u32(header.data() + 24);
            const std::uint32_t offset32 = read_u32(header.data() + 42);

            std::string name(name_size, '\0');
            if (name_size > 0) {
                read_exact(file, &name[0], name_size, "truncated ZIP member name");
            }
            std::vector<unsigned char> extra(extra_size);
            if (extra_size > 0) {
                read_exact(file, reinterpret_cast<char*>(extra.data()), extra_size, "truncated ZIP extra field");
            }
            if (comment_size > 0) {
                file.ignore(comment_size);
                if (!file) {
                    zip_error("truncated ZIP file comment");
                }
            }
            consumed += static_cast<std::uint64_t>(name_size) + extra_size + comment_size;

            ZipMemberInfo info;
            info.name = std::move(name);
            info.flags = read_u16(header.data() + 8);
            info.method = read_u16(header.data() + 10);
            info.compressed_size = compressed32;
            info.uncompressed_size = uncompressed32;
            info.local_header_offset = offset32;
            apply_zip64_extra(info, extra.data(), extra.size(), compressed32, uncompressed32, offset32);
            if (!info.name.empty() && info.name[info.name.size() - 1] != '/') {
                members.push_back(std::move(info));
            }
        }

        if (consumed > central.size) {
            zip_error("ZIP central directory exceeded expected size");
        }
        return members;
    }

    ZipMemberInfo locate_member(const std::string& path, const std::string& member) {
        std::vector<ZipMemberInfo> members = read_members(path);
        for (std::vector<ZipMemberInfo>::const_iterator it = members.begin(); it != members.end(); ++it) {
            if (it->name == member) {
                return *it;
            }
        }
        zip_error("ZIP member not found: " + member);
    }

    std::uint64_t member_data_offset(std::ifstream& file, const ZipMemberInfo& info) {
        std::array<unsigned char, 30> header {};
        seek_abs(file, info.local_header_offset, "failed to seek to ZIP member data");
        read_exact(file, reinterpret_cast<char*>(header.data()), header.size(), "truncated ZIP local file header");
        if (read_u32(header.data()) != LOCAL_HEADER_SIGNATURE) {
            zip_error("invalid ZIP local file header");
        }

        const std::uint16_t name_size = read_u16(header.data() + 26);
        const std::uint16_t extra_size = read_u16(header.data() + 28);
        return info.local_header_offset + 30 + name_size + extra_size;
    }

    void validate_member(const ZipMemberInfo& info) {
        if ((info.flags & FLAG_ENCRYPTED) != 0) {
            zip_error("encrypted ZIP members are not supported: " + info.name);
        }
        if (info.method != METHOD_STORED && info.method != METHOD_DEFLATED) {
            zip_error("unsupported ZIP member compression method: " + info.name);
        }
    }

    class ZipMemberStreamBuf : public std::streambuf {
    public:
        ZipMemberStreamBuf(const std::string& path, const std::string& member)
            : info_(locate_member(path, member)),
              file_(path.c_str(), std::ios::binary),
              compressed_remaining_(info_.compressed_size) {
            if (!this->file_) {
                zip_error("failed to open ZIP archive: " + path);
            }
            validate_member(this->info_);
            const std::uint64_t offset = member_data_offset(this->file_, this->info_);
            seek_abs(this->file_, offset, "failed to seek to ZIP member data");
            this->setg(this->output_.data(), this->output_.data(), this->output_.data());

            if (this->info_.method == METHOD_DEFLATED) {
                std::memset(&this->stream_, 0, sizeof(this->stream_));
                const int32_t status = zng_inflateInit2(&this->stream_, -MAX_WBITS);
                if (status != Z_OK) {
                    throw std::runtime_error("failed to initialize zlib-ng inflater");
                }
                this->inflate_initialized_ = true;
            }
        }

        ZipMemberStreamBuf(const ZipMemberStreamBuf&) = delete;
        ZipMemberStreamBuf& operator=(const ZipMemberStreamBuf&) = delete;

        ~ZipMemberStreamBuf() override {
            if (this->inflate_initialized_) {
                zng_inflateEnd(&this->stream_);
            }
        }

    protected:
        int_type underflow() override {
            if (this->gptr() < this->egptr()) {
                return traits_type::to_int_type(*this->gptr());
            }

            const std::streamsize read = this->read_member_bytes(this->output_.data(), this->output_.size());
            if (read <= 0) {
                return traits_type::eof();
            }
            this->setg(this->output_.data(), this->output_.data(), this->output_.data() + read);
            return traits_type::to_int_type(*this->gptr());
        }

        std::streamsize xsgetn(char* destination, std::streamsize count) override {
            if (count <= 0) {
                return 0;
            }

            std::streamsize total = 0;
            if (this->gptr() < this->egptr()) {
                const std::streamsize available = this->egptr() - this->gptr();
                const std::streamsize copied = (std::min)(available, count);
                std::memcpy(destination, this->gptr(), static_cast<size_t>(copied));
                this->gbump(static_cast<int>(copied));
                destination += copied;
                total += copied;
                count -= copied;
            }

            while (count > 0) {
                const std::streamsize read = this->read_member_bytes(destination, static_cast<size_t>(count));
                if (read <= 0) {
                    break;
                }
                destination += read;
                total += read;
                count -= read;
            }
            return total;
        }

    private:
        ZipMemberInfo info_;
        std::ifstream file_;
        std::uint64_t compressed_remaining_ = 0;
        zng_stream stream_ {};
        bool inflate_initialized_ = false;
        bool inflate_finished_ = false;
        std::array<char, 256 * 1024> input_ {};
        std::array<char, 1024 * 1024> output_ {};

        std::streamsize read_member_bytes(char* destination, size_t count) {
            if (count == 0) {
                return 0;
            }
            if (this->info_.method == METHOD_STORED) {
                return this->read_stored(destination, count);
            }
            return this->read_deflated(destination, count);
        }

        std::streamsize read_stored(char* destination, size_t count) {
            if (this->compressed_remaining_ == 0) {
                return 0;
            }
            const std::uint64_t requested = (std::min<std::uint64_t>)(count, this->compressed_remaining_);
            read_exact(this->file_, destination, requested, "truncated stored ZIP member");
            this->compressed_remaining_ -= requested;
            return static_cast<std::streamsize>(requested);
        }

        bool refill_input() {
            if (this->stream_.avail_in != 0 || this->compressed_remaining_ == 0) {
                return this->stream_.avail_in != 0;
            }

            const std::uint64_t requested = (std::min<std::uint64_t>)(this->input_.size(), this->compressed_remaining_);
            read_exact(this->file_, this->input_.data(), requested, "truncated deflated ZIP member");
            this->compressed_remaining_ -= requested;
            this->stream_.next_in = reinterpret_cast<const uint8_t*>(this->input_.data());
            this->stream_.avail_in = static_cast<uint32_t>(requested);
            return requested != 0;
        }

        std::streamsize read_deflated(char* destination, size_t count) {
            if (this->inflate_finished_) {
                return 0;
            }

            this->stream_.next_out = reinterpret_cast<uint8_t*>(destination);
            const uint32_t requested = static_cast<uint32_t>((std::min<size_t>)(count, (std::numeric_limits<uint32_t>::max)()));
            this->stream_.avail_out = requested;

            while (this->stream_.avail_out > 0 && !this->inflate_finished_) {
                this->refill_input();
                const int32_t status = zng_inflate(&this->stream_, Z_NO_FLUSH);
                if (status == Z_STREAM_END) {
                    this->inflate_finished_ = true;
                    break;
                }
                if (status == Z_BUF_ERROR && this->stream_.avail_in == 0 && this->compressed_remaining_ == 0) {
                    throw std::runtime_error("deflated ZIP member ended before zlib stream end");
                }
                if (status != Z_OK && status != Z_BUF_ERROR) {
                    throw std::runtime_error("failed to inflate ZIP member");
                }
                if (status == Z_BUF_ERROR && this->stream_.avail_in == 0) {
                    break;
                }
            }

            return static_cast<std::streamsize>(requested - this->stream_.avail_out);
        }
    };

    class ZipMemberStream : public std::istream {
    public:
        ZipMemberStream(const std::string& path, const std::string& member)
            : std::istream(nullptr),
              buffer_(path, member) {
            this->rdbuf(&this->buffer_);
        }

    private:
        ZipMemberStreamBuf buffer_;
    };
}

std::unique_ptr<std::istream> open_zip_member_stream(
    const std::string& path,
    const std::string& member
) {
    return std::unique_ptr<std::istream>(new ZipMemberStream(path, member));
}

void init_CSVZip(nb::module_& m) {
    m.def(
        "_zip_members",
        [](const std::string& path) {
            std::vector<ZipMemberInfo> members = read_members(path);
            std::vector<std::string> names;
            names.reserve(members.size());
            for (std::vector<ZipMemberInfo>::const_iterator it = members.begin(); it != members.end(); ++it) {
                names.push_back(it->name);
            }
            return names;
        },
        "Return non-directory member names from a ZIP archive using the native ZIP reader.",
        "path"_a
    );
}
