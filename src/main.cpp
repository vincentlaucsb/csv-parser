/* Command Line Interface for CSV Parser */

# include "csv_parser.h"
# include "util.h"
# include <fstream>
# include <set>
# include <thread>
# include <functional>

using namespace csv_parser;
using std::vector;
using std::deque;
using std::string;
using std::map;

int cli_csv(vector<string>, string);
int cli_json(vector<string>, string delim);
int cli_stat(vector<string>, string);
int cli_grep(vector<string>, string);

void print(string in="", int ntabs=0) {
	for (int i = 0; i < ntabs; i++) {
		std::cout << "\t";
	}

	std::cout << in << std::endl;
}

void print_err(string in) {
	std::cerr << in << std::endl;
}

string rep(string in, int n) {
    // Repeat and concatenate a string multiple times
	string new_str;

	for (int i = 0; i + 1 < n; i++) {
		new_str += in;
	}

	return new_str;
}

template<typename T>
string join(vector<T> in, int a, int b, string delim=" ") {
	string ret_str;

	for (int i = a; i < b; i++) {
		ret_str += in[i].to_string();

		if (i + 1 != b) {
			ret_str += delim;
		}
	}

	return ret_str;
}

template<>
string join(vector<string> in, int a, int b, string delim) {
	string ret_str;

	for (int i = a; i < b; i++) {
		ret_str += in[i];

		if (i + 1 != b) {
			ret_str += delim;
		}
	}

	return ret_str;
}

void print_help() {
    print("CSV Parser");
    print();
    
    // Searching
	print("Search Commands");
	print(rep("-", 80));

    print("head [file]", 1);
    print("Print first 100 lines", 2);
    print();

    print("info [file]", 1);
    print("Print CSV information", 2);
    print();

	print("grep [file] [column number] [regex]", 1);
	print("Print all rows matching a regular expression", 2);
	print();

    print("stat [file]", 1);
    print("Calculate statistics", 2);
    print();
	
    // Reformatting
	print("Reformating Commands");
	print(rep("-", 80));

	print("csv [input] [output]", 1);
	print("Reformat input file as RFC 1480 CSV file", 2);
	print();

	print("csv [input 1] [input 2] ... [output]", 1);
	print("Merge several CSVs into one", 2);
	print();

	print("json [input] [output]", 1);
	print("Newline Delimited JSON Output", 2);
	print();
    
    // General Flags
	print("Flags");
	print(rep("-", 80));
	print(" -d[DELIMITER]:   Specify a delimiter (default: comma)", 1);
}

bool file_exists(string filename, bool throw_err=true) {
    std::ifstream infile;
    infile.open(filename);

    if (!infile.good()) {
		if (throw_err) {
			char err_msg[200];
			snprintf(err_msg, 200, "%s not found", filename.c_str());
			throw string(err_msg);
		}
		else {
			return false;
		}
	}
	else {
		return true;
	}
}

int main(int argc, char* argv[]) {
	string command("");
	vector<string> str_args = {};
	std::set<string> flags = {};
	string delim = "";

	if (argc == 1) {
		print_help();
		return 0;
	}
	else {
		for (int i = 1; i < argc; i++) {
			if (argv[i][0] == '-') {
				// Flag handling
				if (argv[i][1] == 'd') {
					// Delimiter
					if (argv[i][2] == 't') {
						delim = "\t";
					}
					else {
						delim = argv[i][2];
					}
				}
				else {
					// Other flags
					string flag = argv[i];
					flag.erase(0, 1);
					flags.insert(flag);
				}
			}
			else {
				// Assume first string is a command
				if (command.empty()) {
					command = argv[i];
				}
				else {
					str_args.push_back(argv[i]);
				}
			}
		}
	}

	try {
		if (command == "head") {
			file_exists(str_args.at(0));
			head(str_args.at(0), 100, delim);
		}
		else if (command == "grep") {
            return cli_grep(str_args, delim);
		}
        else if (command == "stat") {
            return cli_stat(str_args, delim);
        }
		else if (command == "csv") {
            return cli_csv(str_args, delim);
		}
		else if (command == "json") {
            return cli_json(str_args, delim);
		}
		else {
			std::cerr << "Invalid command." << std::endl;
			return 1;
		}
	}
	catch (string e) {
		// File not found
		std::cerr << e << std::endl;
		return 1;
	}
    catch (std::out_of_range) {
        std::cerr << "Insufficient arguments" << std::endl;
        return 1;
    }
    
    return 0;
}

int cli_stat(vector<string> str_args, string delim) {
    file_exists(str_args.at(0));
    if (delim == "") {
        delim = guess_delim(str_args.at(0));
    }

    CSVStat calc(delim);
    while (!calc.eof) {
        calc.read_csv(str_args.at(0), 50000, false);
        calc.calc();
    }

    vector<string> col_names = calc.get_col_names();
    vector<string> means = round(calc.get_mean());
    vector<string> vars = round(calc.get_variance());
    vector<string> mins = round(calc.get_mins());
    vector<string> maxes = round(calc.get_maxes());
    vector<map<string, int>> counts = calc.get_counts();

    vector<vector<string>*> print_rows = {
        &col_names, &means, &vars, &mins, &maxes
    };

    deque<string> row_names = {
        "", "Mean", "Variance", "Min", "Max"
    };

    // Print basic stats
    print_table(print_rows, row_names);
    print("");

    // Print counts
    for (size_t i = 0; i < col_names.size(); i++) {
        std::cout << "Counts for " << col_names[i] << std::endl;

        map<string, int>::iterator it = counts[i].begin();
        for (size_t j = 0; (j < 10) && it != counts[i].end(); j++) {
            std::cout << it->first << " => " << it->second << std::endl;
            ++it;
        }

        print("");
    }

    return 0;
}

int cli_csv(vector<string> str_args, string delim) {
    if (str_args.size() < 2) {
        std::cerr << "Please specify an input and an output file." << std::endl;
        return 1;
    }
    else if (str_args.size() == 2) {
        // Single CSV input
        file_exists(str_args.at(0));
        CSVCleaner cleaner(delim);
        cleaner.read_csv(str_args.at(0));
        cleaner.to_csv(str_args[1]);
    }
    else {
        // Multiple CSV input
        for (size_t i = 0; i < str_args.size() - 1; i++) {
            file_exists(str_args[i]);
        }

        string outfile = str_args.back();
        str_args.pop_back();

        // Make sure we don't overwrite an existing file
        if (file_exists(outfile, false)) {
            std::cerr << "Output file already exists. Please specify a fresh CSV file to write to." << std::endl;
            return 1;
        }

        merge(outfile, str_args);
    }
    
    return 0;
}

int cli_json(vector<string> str_args, string delim) {
    string outfile;

    if (str_args.size() == 1) {
        file_exists(str_args.at(0));
        outfile = str_args.at(0) + ".ndjson";
    }
    else {
        outfile = str_args.at(1);
    }

    CSVReader reader(delim);

    while (!reader.eof) {
        reader.read_csv(str_args.at(0), 50000, false);
        reader.to_json(outfile, true);
    }

    return 0;
}

int cli_grep(vector<string> str_args, string delim) {
    if (str_args.size() < 3) {
        print_err("Please specify an input file, column number, and regular expression.");
        return 1;
    }

    file_exists(str_args.at(0));
    string reg_exp = join(str_args, 2, str_args.size());
    grep(str_args.at(0), std::stoi(str_args[1]), reg_exp, 500, delim);
    return 0;
}