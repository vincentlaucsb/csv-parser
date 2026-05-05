#pragma once

#include <cstddef>

namespace csv {
    namespace internals {
        struct SpeculativeParseDiagnostics {
            size_t chunks = 0;
            size_t ambiguous_chunks = 0;
            size_t probability_model_chunks = 0;
            size_t record_size_heuristic_chunks = 0;
            size_t assumed_quoted_chunks = 0;
            size_t assumed_unquoted_chunks = 0;
            size_t validation_repairs = 0;

            void merge(const SpeculativeParseDiagnostics& other) noexcept {
                this->chunks += other.chunks;
                this->ambiguous_chunks += other.ambiguous_chunks;
                this->probability_model_chunks += other.probability_model_chunks;
                this->record_size_heuristic_chunks += other.record_size_heuristic_chunks;
                this->assumed_quoted_chunks += other.assumed_quoted_chunks;
                this->assumed_unquoted_chunks += other.assumed_unquoted_chunks;
                this->validation_repairs += other.validation_repairs;
            }
        };
    }
}
