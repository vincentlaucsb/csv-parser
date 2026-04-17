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
}
#endif