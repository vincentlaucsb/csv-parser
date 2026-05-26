#pragma once

#include <string>

namespace csv {
    class DataFrameOptions {
    public:
        DataFrameOptions() = default;

        /** Policy for handling duplicate keys when creating a keyed DataFrame */
        enum class DuplicateKeyPolicy {
            THROW,      // Throw an error if a duplicate key is encountered
            OVERWRITE,  // Overwrite the existing value with the new value
            KEEP_FIRST  // Ignore the new value and keep the existing value
        };

        DataFrameOptions& set_duplicate_key_policy(DuplicateKeyPolicy value) {
            this->duplicate_key_policy = value;
            return *this;
        }

        DuplicateKeyPolicy get_duplicate_key_policy() const {
            return this->duplicate_key_policy;
        }

        DataFrameOptions& set_key_column(const std::string& value) {
            this->key_column = value;
            return *this;
        }

        const std::string& get_key_column() const {
            return this->key_column;
        }

        DataFrameOptions& set_throw_on_missing_key(bool value) {
            this->throw_on_missing_key = value;
            return *this;
        }

        bool get_throw_on_missing_key() const {
            return this->throw_on_missing_key;
        }

    private:
        std::string key_column;

        DuplicateKeyPolicy duplicate_key_policy = DuplicateKeyPolicy::OVERWRITE;

        /** Whether to throw an error if a key column value is missing when creating a keyed DataFrame */
        bool throw_on_missing_key = true;
    };

}
