#pragma once

#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "../common.hpp"

namespace csv {
    namespace internals {
        namespace data_frame {
            // Owning key types need no additional lifetime handling or runtime work.
            template<typename KeyType>
            class KeyStorage {
            public:
                template<typename InvalidateIndex>
                inline void preserve(std::vector<KeyType>&, const InvalidateIndex&) const noexcept {}
            };

            /** Keep established view keys alive when their parsed rows are replaced.
             * Only key bytes are retained, rather than entire obsolete CSV chunks.
             */
            template<>
            class KeyStorage<csv::string_view> {
            public:
                template<typename InvalidateIndex>
                inline void preserve(std::vector<csv::string_view>& keys, const InvalidateIndex& invalidate_index) {
                    if (keys.empty()) {
                        invalidate_index();
                        this->storage_.reset();
                        return;
                    }

                    std::unique_ptr<std::string> storage(new std::string());
                    size_t bytes = 0;
                    for (const auto& key : keys) {
                        if (key.size() > storage->max_size() - bytes) {
                            throw std::length_error("DataFrame key storage is too large");
                        }
                        bytes += key.size();
                    }
                    storage->reserve(bytes);
                    for (const auto& key : keys) {
                        if (!key.empty()) {
                            storage->append(key.data(), key.size());
                        }
                    }

                    // Form views only after the last append. Heap-own the string object
                    // so even its small-string buffer stays stable across frame moves.
                    std::vector<csv::string_view> preserved;
                    preserved.reserve(keys.size());
                    size_t offset = 0;
                    for (const auto& key : keys) {
                        preserved.emplace_back(storage->data() + offset, key.size());
                        offset += key.size();
                    }
                    // Drop index views while both old backing stores are still alive.
                    invalidate_index();
                    keys.swap(preserved);
                    this->storage_ = std::move(storage);
                }

            private:
                std::unique_ptr<std::string> storage_;
            };
        }
    }
}
