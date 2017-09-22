/* Simple JSON Streamer
 * Chops up large JSON documents into single objects
 */

// g++ -std=c++11 -o test _json_streamer.cpp
// ./test

#include <iostream>
#include <queue>
#include <string>
#include <fstream>

namespace csvmorph {
    // Parses JSON one object at a time
    class JSONStreamer {
        public:
            void feed(std::string&);
            std::string pop();
            bool empty();
        private:
            bool currently_parsing = false;
            std::string str_buffer;
            std::queue<std::string> records;
            int left_braces = 0;
            int right_braces = 0;
    };

    // Member functions
    void JSONStreamer::feed(std::string &in) {
        /* Chop up JSON by counting left and right braces
         * This implementation also has the nice side effect of ignoring 
         * newlines and spacing between braces
         */
        
        char * ch;

        for (int i = 0; i < in.length(); i++) {
            ch = &in[i];
            
            if (*ch == '{') {
                this->currently_parsing = true;
                this->left_braces++;
            }
            else if (*ch == '}') {
                this->right_braces++;
                
                if (this->left_braces == this->right_braces) {
                    this->str_buffer += *ch;
                    this->records.push(this->str_buffer);
                    this->str_buffer.clear();
                    this->currently_parsing = false;
                    this->left_braces = 0;
                    this->right_braces = 0;
                }
            }
            
            if (this->currently_parsing) {
                this->str_buffer += *ch;
            }
        }
    }

    std::string JSONStreamer::pop() {
        // Return the first JSON that was parsed
        std::string ret = this->records.front();
        this->records.pop();
        return ret;
    }
    
    bool JSONStreamer::empty() {
        // Return if queue is empty or not
        return this->records.empty();
    }
}