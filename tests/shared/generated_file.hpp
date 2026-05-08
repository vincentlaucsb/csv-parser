#pragma once

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
            : guard_(std::move(filename)) {}

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
        FileGuard guard_;
        bool generated_ = false;
    };
}
