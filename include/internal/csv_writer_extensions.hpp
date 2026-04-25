/** @file
 *  Fast CSVWriter overloads for CSVRow and DataFrame.
 */

#pragma once
#include "csv_row.hpp"
#include "csv_writer.hpp"
#include "data_frame.hpp"

#ifdef CSV_HAS_CXX20
namespace csv {
    template<class OutputStream, char Delim, char Quote, bool Flush>
    DelimWriter<OutputStream, Delim, Quote, Flush>& operator<< 
        (DelimWriter<OutputStream, Delim, Quote, Flush>& writer, const CSVRow& row) {
        return writer << row.to_sv_range();
    }

    /** Overload for writing a DataFrameRow (respects sparse overlay edits). */
    template<class OutputStream, char Delim, char Quote, bool Flush, typename KeyType>
    DelimWriter<OutputStream, Delim, Quote, Flush>& operator<< 
        (DelimWriter<OutputStream, Delim, Quote, Flush>& writer, const DataFrameRow<KeyType>& row) {
        return writer << row.to_sv_range();
    }

    /** Overload for writing a full DataFrame without constructing DataFrameRow proxies. */
    template<class OutputStream, char Delim, char Quote, bool Flush, typename KeyType>
    DelimWriter<OutputStream, Delim, Quote, Flush>& operator<<
        (DelimWriter<OutputStream, Delim, Quote, Flush>& writer, const DataFrame<KeyType>& frame) {
        const bool has_sparse_edits = !frame.edits.empty();

        for (size_t row_index = 0; row_index < frame.rows.size(); ++row_index) {
            const auto& entry = frame.rows[row_index];
            if (!has_sparse_edits) {
                writer << entry.second;
                continue;
            }

            const auto row_edit_it = frame.edits.find(row_index);
            if (row_edit_it == frame.edits.end() || row_edit_it->second.empty()) {
                writer << entry.second;
                continue;
            }

            const CSVRow& row = entry.second;
            const auto& row_edits = row_edit_it->second;
            writer.write_indexed_row(row.size(), [&row, &row_edits](size_t i) -> csv::string_view {
                auto edited = row_edits.find(i);
                if (edited != row_edits.end()) {
                    return csv::string_view(edited->second);
                }

                return row[i].template get<csv::string_view>();
            });
        }

        return writer;
    }
}
#endif
