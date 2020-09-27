#include "col_names.hpp"

namespace csv {
    namespace internals {
        CSV_INLINE std::vector<std::string> ColNames::get_col_names() const {
            return this->col_names;
        }

        CSV_INLINE void ColNames::set_col_names(const std::vector<std::string>& cnames) {
            this->col_names = cnames;

            for (size_t i = 0; i < cnames.size(); i++) {
                this->col_pos[cnames[i]] = i;
            }
        }

        CSV_INLINE int ColNames::index_of(csv::string_view col_name) const {
            auto pos = this->col_pos.find(col_name.data());
            if (pos != this->col_pos.end())
                return (int)pos->second;

            return CSV_NOT_FOUND;
        }

        CSV_INLINE size_t ColNames::size() const noexcept {
            return this->col_names.size();
        }

    }
}