#include <algorithm>
#include <cctype>
#include "col_names.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE std::vector<std::string> ColNames::get_col_names() const {
            return this->col_names;
        }

        CSV_INLINE void ColNames::set_col_names(const std::vector<std::string>& cnames) {
            this->col_names = cnames;
            this->col_pos.clear();

            for (size_t i = 0; i < cnames.size(); i++) {
                if (this->_policy == csv::ColumnNamePolicy::CASE_INSENSITIVE) {
                    // For case-insensitive lookup, cache a lowercase version
                    // of the column name in the map
                    std::string lower(cnames[i]);
                    std::transform(lower.begin(), lower.end(), lower.begin(),
                        [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                    this->col_pos[lower] = i;
                } else {
                    this->col_pos[cnames[i]] = i;
                }
            }
        }

        CSV_INLINE int ColNames::index_of(csv::string_view col_name) const {
            if (this->_policy == csv::ColumnNamePolicy::CASE_INSENSITIVE) {
                std::string lower(col_name);
                std::transform(lower.begin(), lower.end(), lower.begin(),
                    [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                auto pos = this->col_pos.find(lower);
                if (pos != this->col_pos.end())
                    return (int)pos->second;
                return CSV_NOT_FOUND;
            }

            auto pos = this->col_pos.find(col_name.data());
            if (pos != this->col_pos.end())
                return (int)pos->second;

            return CSV_NOT_FOUND;
        }

        CSV_INLINE void ColNames::set_policy(csv::ColumnNamePolicy policy) {
            this->_policy = policy;
        }

        CSV_INLINE size_t ColNames::size() const noexcept {
            return this->col_names.size();
        }

        CSV_INLINE const std::string& ColNames::operator[](size_t i) const {
            if (i >= this->col_names.size())
                throw std::out_of_range("Column index out of bounds.");

            return this->col_names[i];
        }
    }
}