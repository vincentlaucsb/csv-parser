/* Command Line Interface for CSV Parser */

# include "csv_parser.h"
# include <iostream>
# include <fstream>
# include <vector>
# include <string>
# include <set>

using namespace csv_parser;

void print_help() {
    std::cout << "CSV Parser" << std::endl;
    std::cout << "-----" << std::endl;
    std::cout << "Flags" << std::endl;
    std::cout << " -csv: CSV Output" << std::endl;
    std::cout << " -json: NDJSON Output" << std::endl;
    std::cout << " -d[DELIMITER]:   Specify a delimiter (default: comma)" << std::endl;
}

int file_exists(std::string filename) {
    std::ifstream infile;
    infile.open(filename);
    if (infile.good()) {
        return 0;
    } else {
        std::cerr << "File not found." << std::endl;
        return 1;
    }
}

int main (int argc, char* argv[]) {
    std::vector<std::string> str_args = {};
    std::set<std::string> flags = {};
    std::string delim = ",";
    
    if (argc == 1) {
        print_help();
    } else {
        for (int i = 1; i < argc; i++) {
            if (argv[i][0] == '-') {
                // Flag handling
                if (argv[i][1] == 'd') {
                    // Delimiter
                    if (argv[i][2] == 't') {
                        delim = "\t";
                    } else {
                        delim = argv[i][2];                        
                    }
                } else {
                    // Other flags
                    std::string flag = argv[i];
                    flag.erase(0, 1);
                    flags.insert(flag);
                }
            } else {
                str_args.push_back(argv[i]);
            }
        }
    }
    
    if (flags.find("csv") != flags.end()) {
        if (str_args.size() < 2) {
            std::cerr << "Please specify an input and an output file." << std::endl;
            return 1;        
        }
        
        file_exists(str_args[0]);
        
        CSVCleaner cleaner(delim);
        cleaner.read_csv(str_args[0]);
        cleaner.to_csv(str_args[1]);
    } else if (flags.find("json") != flags.end()) {
        std::string outfile;
        
        if (str_args.size() == 1) {
            outfile = str_args[0] + ".ndjson";
        } else if (str_args.size() == 2) {
            outfile = str_args[1];
        } else {
            std::cerr << "Too few arguments." << std::endl;
        }
        
        file_exists(str_args[0]);
        
        CSVReader reader(delim);
        reader.read_csv(str_args[0]);
        reader.to_json(outfile);
    }
    
    return 0;
}