#pragma once
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>

#include "common.hpp"

namespace csv {
    namespace internals {
        struct ColNames;
        using ColNamesPtr = std::shared_ptr<ColNames>;

        /** @struct ColNames
             *  A data structure for handling column name information.
             *
             *  These are created by CSVReader and passed (via smart pointer)
             *  to CSVRow objects it creates, thus
             *  allowing for indexing by column name.
             */
        struct ColNames {
        public:
            ColNames() = default;
            ColNames(const std::vector<std::string>& names) {
                set_col_names(names);
            }

            std::vector<std::string> get_col_names() const;
            void set_col_names(const std::vector<std::string>&);
            int index_of(csv::string_view) const;

            bool empty() const noexcept { return this->col_names.empty(); }
            size_t size() const noexcept;

        private:
            std::vector<std::string> col_names;
            std::unordered_map<std::string, size_t> col_pos;
        };
    }
}