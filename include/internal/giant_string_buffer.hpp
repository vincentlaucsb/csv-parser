#pragma once
#include <memory>

#include "compatibility.hpp" // For string view

namespace csv {
    namespace internals {
        /** Class for reducing number of new string malloc() calls */
        class GiantStringBuffer {
        public:
            csv::string_view get_row();
            size_t size() const;
            std::string* get() const;
            std::string* operator->() const;
            std::shared_ptr<std::string> buffer = std::make_shared<std::string>();
            void reset();

        private:
            size_t current_end = 0;
        };
    }
}