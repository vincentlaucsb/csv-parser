/** @file */
/* Command Line Interface for CSV Parser */

# include "csv_parser.h"
# include "print.h"
# include "getargs.h"
# include <set>

using namespace csv_parser;
using std::vector;
using std::deque;
using std::string;
using std::map;

int cli_info(string);
int cli_csv(vector<string>);
int cli_sample(vector<string>);
int cli_json(vector<string>);
int cli_stat(vector<string>);
int cli_grep(vector<string>);
int cli_rearrange(vector<string>);
int cli_sql(vector<string>);
int cli_join(vector<string>);

string rep(string in, int n) {
    // Repeat and concatenate a string multiple times
    string new_str;

    for (int i = 0; i + 1 < n; i++) {
        new_str += in;
    }

    return new_str;
}

void print(string in="", int ntabs=0) {
	for (int i = 0; i < ntabs; i++)
        std::cout << rep(" ", 2); // 2 spaces per "tab"
	std::cout << in << std::endl;
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
    print("");

    print("Basic Usage");
    print(rep("-", 80));
    print("csv-parser [command] [arguments]");
    print(" - If no command is specified, the parser pretty prints the file to the terminal");
    print(" - Escape spaces with quotes");
    print();

    // Searching
    print("Search Commands");
    print(rep("-", 80));

    print("info [file]", 1);
    print("Display basic CSV information", 2);
    print();

    print("grep [file] [column name/number] [regex]", 1);
    print("Print all rows matching a regular expression", 2);
    print();

    print("stat [file]", 1);
    print("Calculate statistics", 2);
    print();

    // Reformatting
    print("Reformating Commands");
    print(rep("-", 80));

    print("csv [input 1] [input 2] ... [output]", 1);
    print("Reformat one or more input files into a single RFC 1480 compliant CSV file", 2);
    print();

    /*
    print("sample [input] [output] [n]", 1);
    print("Take a random sample (with replacement) of n rows", 2);
    print();
    */

    print("json [input] [output]", 1);
    print("Newline Delimited JSON Output", 2);
    print();

    // Advanced
    print("Advanced");
    print(rep("-", 80));

    print("sql [input] [output]", 1);
    print("Transform CSV file into a SQLite3 database", 2);
    print();

    print("join [input 1] [input 2]", 1);
    print("Join two CSV files on their common fields", 2);
}

bool file_exists(string filename) {
    std::ifstream infile(filename);
    bool exists = infile.good();
    infile.close();
    return exists;
}

int main(int argc, char* argv[]) {
    string command;
    string delim = "";
	vector<string> str_args;
    vector<string> flags;

	if (argc == 1) {
		print_help();
	}
    else {
        int fail = getargs(argc, argv, str_args, flags);
        if (fail == 1) {
            std::cerr << "Invalid syntax" << std::endl;
            return 1;
        }
        else {
            command = str_args[0];
            str_args.erase(str_args.begin());
        }
    }

	try {
        if (command == "info")
            return cli_info(str_args.at(0));
        else if (command == "grep")
            return cli_grep(str_args);
        else if (command == "stat")
            return cli_stat(str_args);
		else if (command == "csv")
            return cli_csv(str_args);
		else if (command == "json")
            return cli_json(str_args);
        else if (command == "rearrange")
            return cli_rearrange(str_args);
        else if (command == "sql")
            return cli_sql(str_args);
        else if (command == "join")
            return cli_join(str_args);
		else
            head(command, 100); // Assume first arg is a filename
	}
    catch (std::runtime_error e) {
        std::cerr << e.what() << std::endl;
        return 1;
    }
    catch (std::out_of_range) {
        std::cerr << "Insufficient arguments" << std::endl;
        return 1;
    }
    return 0;
}

int cli_stat(vector<string> str_args) {
    std::string filename = str_args.at(0);
    file_exists(str_args.at(0));
   
    CSVStat calc(guess_delim(filename));
    (&calc)->bad_row_handler = &print_record;
    vector<string> row;

    while(calc.read_row(filename, row))
        calc.calc();

    vector<string> col_names = calc.get_col_names();
    vector<map<string, int>> counts = calc.get_counts();
    vector<vector<string>> print_rows = {
        col_names,
        round(calc.get_mean()),
        round(calc.get_variance()),
        round(calc.get_mins()),
        round(calc.get_maxes())
    };
    vector<string> row_names = { "", "Mean", "Variance", "Min", "Max" };

    // Print basic stats
    print_table(print_rows, -1, row_names);
    print("");

    // Print counts
    for (size_t i = 0; i < col_names.size(); i++) {
        std::cout << "Counts for " << col_names[i] << std::endl;
        map<string, int> temp = top_n_values(counts[i], 10);

        for (auto it = temp.begin(); it != temp.end(); ++it) {
            print_rows.push_back(
                vector<string>({ it->first, std::to_string(it->second) }));
        }

        print_table(print_rows);
        print("");
    }

    return 0;
}

int cli_info(string filename) {
    CSVFileInfo info = get_file_info(filename);
    vector<vector<string>> records;
    auto info_p = &info;
    print(info_p->filename);
    
    records.push_back({"Delimiter", info_p->delim});
    records.push_back({"Rows", std::to_string(info_p->n_rows) });
    records.push_back({"Columns", std::to_string(info_p->n_cols) });

    for (size_t i = 0; i < info_p->col_names.size(); i++)
        records.push_back({"[" + std::to_string(i) + "]", info_p->col_names[i]});

    print_table(records, -1);
    return 0;
}

int cli_csv(vector<string> str_args) {
    if (str_args.size() < 2) {
        throw std::runtime_error("Please specify an input and an output file.");
    }
    else if (str_args.size() == 2) {
        reformat(str_args.at(0), str_args.at(1));  // Single CSV input
    }
    else {
        string outfile = str_args.back();
        str_args.pop_back();
        if (file_exists(outfile))
            throw std::runtime_error("Output file already exists. Please specify "
                "a fresh CSV file to write to.");
        merge(outfile, str_args);
    }
    
    return 0;
}


int cli_json(vector<string> str_args) {
    string filename = str_args.at(0);
    string outfile(filename + ".ndjson");
    if (str_args.size() > 1)
        outfile = str_args[1];

    CSVReader reader(guess_delim(filename));
    while (!reader.eof) {
        reader.read_csv(filename, ITERATION_CHUNK_SIZE, false);
        reader.to_json(outfile, true);
        reader.clear();
    }

    return 0;
}

int cli_grep(vector<string> str_args) {
    if (str_args.size() < 3)
        throw std::runtime_error("Please specify an input file,"
            "column number, and regular expression.");

    string filename = str_args.at(0);
    string delim = guess_delim(filename);
    string reg_exp = join(str_args, 2, str_args.size());

    try {
        size_t col = std::stoi(str_args[1]);
        size_t n_cols = get_col_names(filename, delim).size();

        // Assert column position exists
        if (col + 1 > n_cols) {
            throw std::runtime_error(filename + " only has " +
                std::to_string(n_cols) + " columns");
        }

        grep(filename, col, reg_exp, 500, delim);
    }
    catch (std::invalid_argument) {
        int col = col_pos(filename, str_args[1]);
        if (col == -1)
            throw std::runtime_error("Could not find a column named " + str_args[1]);
        else
            grep(filename, col, reg_exp, 500, delim);
    }
    
    return 0;
}

int cli_rearrange(vector<string> str_args) {
    string filename = str_args.at(0);
    string outfile = str_args.at(1);
    string delim = guess_delim(filename);
    vector<int> columns = {};
    int col_index = -1;

    // Resolve column arguments
    for (size_t i = 2; i < str_args.size(); i++) {
        try {
            columns.push_back(std::stoi(str_args[i]));
        }
        catch (std::invalid_argument) {
            col_index = col_pos(filename, str_args[i]);
            if (col_index == -1)
                throw std::runtime_error("Could not find a column named " + str_args[i]);
            else
                columns.push_back(col_index);
        }
    }

    CSVReader reader(delim, "\"", 0, columns);
    CSVWriter writer(outfile);
    bool write_col_names = false;
    vector<string> row;

    while (reader.read_row(filename, row)) {
        if (!write_col_names)
            writer.write_row(reader.get_col_names());
        writer.write_row(row);
    }

    writer.close();
    return 0;
}

int cli_sql(vector<string> str_args) {
    string csv_file = str_args.at(0);
    string db_file = str_args.at(1);
    csv_to_sql(csv_file, db_file);
    return 0;
}

int cli_join(vector<string> str_args) {
    string file1 = str_args.at(0), file2 = str_args.at(1);
    string outfile = str_args.at(2);
    string column1(""), column2("");

    if (str_args.size() >= 4)
        column1 = str_args.at(3);
    if (str_args.size() >= 5)
        column2 = str_args.at(4);

    csv_join(file1, file2, outfile, column1, column2);
    return 0;
}