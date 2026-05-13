#pragma once

#include <cstdlib>
#include <fstream>
#include <string>
#include <utility>

#include "file_guard.hpp"

namespace csv_test {
    /**
     * Memoized generated-file fixture for Catch2 SECTION matrices.
     *
     * Catch2 reruns the full test body for every SECTION, so expensive file
     * generation belongs behind a static fixture when multiple parser paths
     * consume the same bytes.
     */
    class GeneratedFile {
    public:
        explicit GeneratedFile(std::string filename)
            : guard_(temp_filename(std::move(filename))) {}

        template<typename Generator>
        const std::string& path(Generator&& generator) {
            if (!this->generated_) {
                std::ofstream out(this->guard_.filename, std::ios::binary);
                generator(out);
                this->generated_ = true;
            }

            return this->guard_.filename;
        }

    private:
        static std::string env_value(const char* name) {
#if defined(_MSC_VER)
            char* value = nullptr;
            size_t size = 0;
            if (_dupenv_s(&value, &size, name) != 0 || value == nullptr) {
                return std::string();
            }

            std::string result(value);
            std::free(value);
            return result;
#else
            const char* value = std::getenv(name);
            return value ? std::string(value) : std::string();
#endif
        }

        static std::string temp_filename(std::string filename) {
            if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
                return filename;
            }

            std::string temp_dir = env_value("TMPDIR");
            if (temp_dir.empty()) {
                temp_dir = env_value("TEMP");
            }
            if (temp_dir.empty()) {
                temp_dir = env_value("TMP");
            }
            if (temp_dir.empty()) {
                return filename;
            }

            std::string path(std::move(temp_dir));
            const char last = path.empty() ? '\0' : path[path.size() - 1];
            if (last != '/' && last != '\\') {
#if defined(_WIN32)
                path.push_back('\\');
#else
                path.push_back('/');
#endif
            }
            path += filename;
            return path;
        }

        FileGuard guard_;
        bool generated_ = false;
    };
}
