/* Command Line Interface for CSV Parser */

# include "csv_parser.h"
# include <iostream>
# include <fstream>
# include <vector>
# include <string>
# include <set>

using namespace csv_parser;

void print(std::string in="", int ntabs=0) {
	for (int i = 0; i < ntabs; i++) {
		std::cout << "\t";
	}

	std::cout << in << std::endl;
}

void print_err(std::string in) {
	std::cerr << in << std::endl;
}

std::string rep(std::string in, int n) {
	std::string new_str;

	for (int i = 0; i + 1 < n; i++) {
		new_str += in;
	}

	return new_str;
}

template<typename T>
std::string join(std::vector<T> in, int a, int b, std::string delim=" ") {
	std::string ret_str;

	for (int i = a; i < b; i++) {
		ret_str += in[i].to_string();

		if (i + 1 != b) {
			ret_str += delim;
		}
	}

	return ret_str;
}

template<>
std::string join(std::vector<std::string> in, int a, int b, std::string delim) {
	std::string ret_str;

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

	print("Search Commands");
	print(rep("-", 80));
	print("head:   Print first 100 lines", 1);
	print("grep [file] [column number] [regex]", 1);
	print("Print all rows matching a regular expression", 2);
	print();
	
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

	print("Flags");
	print(rep("-", 80));
	print(" -d[DELIMITER]:   Specify a delimiter (default: comma)", 1);
}

bool file_exists(std::string filename, bool throw_err=true) {
    std::ifstream infile;
    infile.open(filename);

    if (!infile.good()) {
		if (throw_err) {
			char err_msg[200];
			snprintf(err_msg, 200, "%s not found", filename.c_str());
			throw std::string(err_msg);
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
	std::string command("");
	std::vector<std::string> str_args = {};
	std::set<std::string> flags = {};
	std::string delim = ",";

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
					std::string flag = argv[i];
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
			file_exists(str_args[0]);
			head(str_args[0]);
		}
		else if (command == "grep") {
			if (str_args.size() < 3) {
				print_err("Please sepcify an input file, column number, and regular expression.");
				return 1;
			}

			file_exists(str_args[0]);
			std::string reg_exp = join(str_args, 2, str_args.size());

			int ommited = grep(str_args[0], std::stoi(str_args[1]), reg_exp);
			if (ommited > 0) {
				print("First 500 results were printed. Others ommited.");
			}
		}
		else if (command == "csv") {
			if (str_args.size() < 2) {
				std::cerr << "Please specify an input and an output file." << std::endl;
				return 1;
			}
			else if (str_args.size() == 2) {
				// Single CSV input
				file_exists(str_args[0]);
				CSVCleaner cleaner(delim);
				cleaner.read_csv(str_args[0]);
				cleaner.to_csv(str_args[1]);
			}
			else {
				// Multiple CSV input
				for (size_t i = 0; i < str_args.size() - 1; i++) {
					file_exists(str_args[i]);
				}

				std::string outfile = str_args.back();
				str_args.pop_back();

				// Make sure we don't overwrite an existing file
				if (file_exists(outfile, false)) {
					std::cerr << "Output file already exists. Please specify a fresh CSV file to write to." << std::endl;
					return 1;
				}

				merge(outfile, str_args);
			}
		}
		else if (command == "json") {
			std::string outfile;

			if (str_args.size() == 1) {
				outfile = str_args[0] + ".ndjson";
			}
			else if (str_args.size() == 2) {
				outfile = str_args[1];
			}
			else {
				std::cerr << "Too few arguments." << std::endl;
				return 1;
			}

			file_exists(str_args[0]);

			CSVReader reader(delim);
			reader.read_csv(str_args[0]);
			reader.to_json(outfile);
		}
		else {
			std::cerr << "Invalid command." << std::endl;
			return 1;
		}
	}
	catch (std::string e) {
		// File not found
		std::cerr << e << std::endl;
		return 1;
	}
    
    return 0;
}