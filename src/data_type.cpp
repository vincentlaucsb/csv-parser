# include "csv_parser.h"

namespace csv {
    namespace helpers {
        /** @file */

        DataType data_type(std::string &in) {
            /** Distinguishes numeric from other text values. Used by various 
             *  type casting functions, like csv_parser::CSVReader::read_row()
             * 
             *  #### Return
             *   - 0:  If null (empty string)
             *   - 1:  If string
             *   - 2:  If int
             *   - 3:  If float
             *
             *  #### Rules
             *   - Leading and trailing whitespace ("padding") ignored
             *   - A string of just whitespace is NULL
             *  
             *  @param[in] in String value to be examined
             */

            // Empty string --> NULL
            if (in.size() == 0)
                return _null;

            bool ws_allowed = true;
            bool neg_allowed = true;
            bool dot_allowed = true;
            bool digit_allowed = true;
            bool has_digit = false;
            bool prob_float = false;

            for (size_t i = 0, ilen = in.size(); i < ilen; i++) {
                switch (in[i]) {
                case ' ':
                    if (!ws_allowed) {
                        if (isdigit(in[i - 1])) {
                            digit_allowed = false;
                            ws_allowed = true;
                        }
                        else {
                            // Ex: '510 123 4567'
                            return _string;
                        }
                    }
                    break;
                case '-':
                    if (!neg_allowed) {
                        // Ex: '510-123-4567'
                        return _string;
                    }
                    else {
                        neg_allowed = false;
                    }
                    break;
                case '.':
                    if (!dot_allowed) {
                        return _string;
                    }
                    else {
                        dot_allowed = false;
                        prob_float = true;
                    }
                    break;
                default:
                    if (isdigit(in[i])) {
                        if (!digit_allowed) {
                            return _string;
                        }
                        else if (ws_allowed) {
                            // Ex: '510 456'
                            ws_allowed = false;
                        }
                        has_digit = true;
                    }
                    else {
                        return _string;
                    }
                }
            }

            // No non-numeric/non-whitespace characters found
            if (has_digit) {
                if (prob_float) {
                    return _float;
                }
                else {
                    return _int;
                }
            }
            else {
                // Just whitespace
                return _null;
            }
        }
    }
}