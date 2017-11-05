/* Function for distinguishing numeric from text values
 */

# include "csv_parser.h"
# include <iostream>
# include <string>

namespace csv_parser {  
    int data_type(std::string &in) {
        /*
        Returns:
            0:  If null
            1:  If string
            2:  If int
            3:  If float
            
        Rules:
            - Leading and trailing whitespace ("padding") ignored
        */
        
        // Empty string --> NULL
        if (in.size() == 0) {
            return 0;
        }
        
        bool ws_allowed = true;
        bool neg_allowed = true;
        bool dot_allowed = true;
        bool digit_allowed = true;
        bool prob_float = false;
        
        for (size_t i = 0, ilen = in.size(); i < ilen; i++) {
            switch (in[i]) {
                case ' ':
                    if (!ws_allowed) {
                        if (isdigit(in[i - 1])) {
                            digit_allowed = false;
                            ws_allowed = true;
                        } else {
                            // Ex: '510 123 4567'
                            return 1;
                        }
                    }
                    break;
                case '-':
                    if (!neg_allowed) {
                        // Ex: '510-123-4567'
                        return 1;
                    } else {
                        neg_allowed = false;
                    }
                    break;
                case '.':
                    if (!dot_allowed) {
                        return 1;
                    } else {
                        dot_allowed = false;
                        prob_float = true;
                    }
                    break;
                default:
                    if (isdigit(in[i])) {
                        if (!digit_allowed) {
                            return 1;
                        } else if (ws_allowed) {
                            // Ex: '510 456'
                            ws_allowed = false;
                        }
                    } else {
                        return 1;
                    }
            }
        }
        
        if (prob_float) {
            return 3;
        } else {
            return 2;
        }
    }
}