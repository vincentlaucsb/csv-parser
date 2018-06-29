#pragma once
// Auxiliary data structures for CSV parser

#include "data_type.h"
#include <vector>
#include <string>
#include <string_view>
#include <unordered_map> // For ColNames
#include <memory> // For CSVField
#include <limits> // For CSVField

namespace csv {
    struct ColNames {
        ColNames(const std::vector<std::string>&);
        std::vector<std::string> col_names;
        std::unordered_map<std::string, size_t> col_pos;

        std::vector<std::string> get_col_names() const;
        size_t size() const;
    };

    class CSVRow {
        struct CSVField {
            CSVField(std::string_view _sv) : sv(_sv) { };
            std::string_view sv;

            bool operator==(const std::string&) const;
            template<typename T>
            T get() {
                static_assert(1 == 2, "Not supported");
            };
        };

    public:
        CSVRow() = default;
        CSVRow::CSVRow(std::string&& _str, std::vector<size_t>&& _splits,
            std::shared_ptr<ColNames> _cnames = nullptr) :
            row_str(std::move(_str)),
            splits(std::move(_splits)),
            col_names(_cnames)
        {};

        bool empty() const { return this->row_str.empty(); }
        size_t size() const;
        std::string_view get_string_view(size_t n) const;
        CSVField operator[](size_t n) const;
        CSVField operator[](const std::string&) const;
        operator std::vector<std::string>() const;

    private:
        std::shared_ptr<ColNames> col_names = nullptr;
        std::string row_str;
        std::vector<size_t> splits;
    };

    template<>
    inline std::string CSVRow::CSVField::get<std::string>() {
        return std::string(this->sv);
    }

    template<>
    inline std::string_view CSVRow::CSVField::get<std::string_view>() {
        return this->sv;
    }

    template<>
    inline long long CSVRow::CSVField::get<long long>() {
        long double temp;
        if (helpers::data_type(this->sv, &temp) >= CSV_STRING)
            return static_cast<long long>(temp);
    }

    template<>
    inline double CSVRow::CSVField::get<double>() {
        long double temp;
        if (helpers::data_type(this->sv, &temp) >= CSV_STRING)
            return static_cast<double>(temp);
    }
}