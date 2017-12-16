/** @file */
/* Command Line Interface for CSV Parser */

// b is number of "tabs" (2 spaces) preceeding b
#define print(a) std::cout << a << std::endl
#define hrule(a) std::cout << (rep("-", a)) << std::endl;
#define skip std::cout << std::endl
#define indent(a, b) std::cout << rep("  ", b) << a << std::endl

#include "csv_parser.h"
#include "print.h"
#include "getargs.h"
#include <set>

using namespace csv_parser;
using std::vector;
using std::deque;
using std::string;
using std::unordered_map;

int cli_info(string);
int cli_csv(deque<string>);
int cli_sample(deque<string>);
int cli_json(deque<string>);
int cli_stat(deque<string>);
int cli_grep(deque<string>);
int cli_rearrange(deque<string>, deque<string>);
int cli_sql(deque<string>);
int cli_query(deque<string>);
int cli_join(deque<string>);

string rep(string in, int n) {
    // Repeat and concatenate a string multiple times
    string new_str;

    for (int i = 0; i + 1 < n; i++) {
        new_str += in;
    }

    return new_str;
}

string join(deque<string> in, int a, int b, string delim=" ") {
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
    skip;

    print("Basic Usage");
    hrule(80);
    print("csv-parser [command] [arguments]");
    print(" - If no command is specified, the parser pretty prints the file to the terminal");
    print(" - Escape spaces with quotes");
    skip;

    // Searching
    print("Search Commands");
    hrule(80);

    indent("info [file]", 1);
    indent("Display basic CSV information", 2);
    skip;

    indent("grep [file] [column name/number] [regex]", 1);
    indent("Print all rows matching a regular expression", 2);
    skip;

    indent("stat [file]", 1);
    indent("Calculate statistics", 2);
    skip;

    // Reformatting
    print("Reformating Commands");
    hrule(80);

    indent("csv [input 1] [input 2] ... [output]", 1);
    indent("Reformat one or more input files into a "
           "single RFC 1480 compliant CSV file", 2);
    skip;

    /*
    print("sample [input] [output] [n]", 1);
    print("Take a random sample (with replacement) of n rows", 2);
    print();
    */

    indent("json [input] [output]", 1);
    indent("Newline Delimited JSON Output", 2);
    skip;

    // Advanced
    print("Advanced");
    hrule(80);

    indent("sql [input] [output]", 1);
    indent("Transform CSV file into a SQLite3 database", 2);
    skip;

    indent("query [filename] [query]", 1);
    indent("Query a SQLite database", 2);
    skip;

    indent("join [input 1] [input 2]", 1);
    indent("Join two CSV files on their common fields", 2);
    skip;
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
	deque<string> str_args;
    deque<string> flags;

	if (argc == 1) {
		print_help();
        return 0;
	}
    else {
        int fail = getargs(argc, argv, str_args, flags);
        if (fail == 1) {
            std::cerr << "Invalid syntax" << std::endl;
            return 1;
        }
        else {
            if (std::find(flags.begin(), flags.end(), "stdin") != flags.end()) {
                // Grab stuff from standard input
                std::ofstream temp_file("temp.txt", std::ios_base::binary);
                std::string temp;

                while (std::getline(std::cin, temp)) {
                    if (temp.empty())
                        break;

                    temp_file << temp;
                    temp_file << "\n";
                    temp.clear();
                }

                str_args.push_front("temp.txt");
            }

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
            return cli_rearrange(str_args, flags);
        else if (command == "sql")
            return cli_sql(str_args);
        else if (command == "query")
            return cli_query(str_args);
        else if (command == "join")
            return cli_join(str_args);
		else
            extra::head(command, 100); // Assume first arg is a filename
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

int cli_stat(deque<string> str_args) {
    std::string filename = str_args.at(0);
    file_exists(str_args.at(0));
   
    CSVStat calc;
    (&calc)->bad_row_handler = &helpers::print_record;
    calc.calc_csv(filename);

    vector<string> col_names = calc.get_col_names();
    vector<unordered_map<string, int>> counts = calc.get_counts();
    vector<vector<string>> print_rows = {
        col_names,
        helpers::round(calc.get_mean()),
        helpers::round(calc.get_variance()),
        helpers::round(calc.get_mins()),
        helpers::round(calc.get_maxes())
    };
    vector<string> row_names = { "", "Mean", "Variance", "Min", "Max" };

    // Introduction
    std::cout << filename << " - " << "Full Statistics Report" << std::endl;
    print(rep("=", 120)); skip;
    
    // Print basic stats
    print("Summary Statistics");
    hrule(120); skip;
    helpers::print_table(print_rows, -1, row_names);
    skip;

    /**
     * Print counts
     *
     * Formatting:
     *  - Stack counts horizontally
     *  - Let print_table() do the line breaking
     */

    // Reserve space for 10 rows + 1 header
    for (int i = 0; i < 11; i++)
        print_rows.push_back({});

    // Loop over columns
    print("Frequency Counts - Top 10 Most Common Values");
    hrule(120); skip;

    auto current_row = print_rows.begin();

    for (size_t i = 0; i < col_names.size(); i++) {
        current_row = print_rows.begin();

        // Add header
        current_row->push_back(col_names[i]);

        // Add counts
        unordered_map<string, int> temp = helpers::top_n_values(counts[i], 10);

        size_t j = 0;
        for (auto it = temp.begin(); it != temp.end(); ++it) {
            current_row++;
            current_row->push_back(it->first + ":   " + std::to_string(it->second));
            j++;
        }

        // If there are less than 10 items in this column, add filler
        for (; j < 10; j++) {
            current_row++;
            current_row->push_back("");
        }
    }

    helpers::print_table(print_rows, -1);
    return 0;
}

int cli_info(string filename) {
    CSVFileInfo info = get_file_info(filename);
    vector<vector<string>> records;
    std::string delim = "";
    delim += info.delim;

    print(info.filename);
    records.push_back({"Delimiter", delim});
    records.push_back({"Rows", std::to_string(info.n_rows) });
    records.push_back({"Columns", std::to_string(info.n_cols) });

    for (size_t i = 0; i < info.col_names.size(); i++)
        records.push_back({"[" + std::to_string(i) + "]", info.col_names[i]});

    helpers::print_table(records, -1);
    return 0;
}

int cli_csv(deque<string> str_args) {
    if (str_args.size() < 2) {
        throw std::runtime_error("Please specify an input and an output file.");
    }
    else if (str_args.size() == 2) {
        extra::reformat(str_args.at(0), str_args.at(1));  // Single CSV input
    }
    else {
        string outfile = str_args.back();
        str_args.pop_back();
        vector<string> temp;
        temp.assign(str_args.begin(), str_args.end());

        if (file_exists(outfile))
            throw std::runtime_error("Output file already exists. Please specify "
                "a fresh CSV file to write to.");
        extra::merge(outfile, temp);
    }
    
    return 0;
}


int cli_json(deque<string> str_args) {
    string filename = str_args.at(0);
    string outfile(filename + ".ndjson");
    if (str_args.size() > 1)
        outfile = str_args[1];

    CSVReader reader(filename);
    while (!reader.eof) {
        reader.read_csv(filename, ITERATION_CHUNK_SIZE, false);
        reader.to_json(outfile, true);
        reader.clear();
    }

    return 0;
}

int cli_grep(deque<string> str_args) {
    if (str_args.size() < 3)
        throw std::runtime_error("Please specify an input file,"
            "column number, and regular expression.");

    string filename = str_args.at(0);
    string reg_exp = join(str_args, 2, str_args.size());

    try {
        size_t col = std::stoi(str_args[1]);
        size_t n_cols = get_col_names(filename).size();

        // Assert column position exists
        if (col + 1 > n_cols) {
            throw std::runtime_error(filename + " only has " +
                std::to_string(n_cols) + " columns");
        }

        extra::grep(filename, col, reg_exp, 500);
    }
    catch (std::invalid_argument) {
        int col = get_col_pos(filename, str_args[1]);
        if (col == -1)
            throw std::runtime_error("Could not find a column named " + str_args[1]);
        else
            extra::grep(filename, col, reg_exp, 500);
    }
    
    return 0;
}

int cli_rearrange(deque<string> str_args, deque<string> flags) {
    string filename = str_args.at(0);
    string outfile;
    bool stdout_ = false;

    if (std::find(flags.begin(), flags.end(), "stdout") != flags.end())
        stdout_ = true;
    else
        outfile = str_args.at(1);
    vector<int> columns = {};
    int col_index = -1;

    // Resolve column arguments
    for (size_t i = 2; i < str_args.size(); i++) {
        try {
            columns.push_back(std::stoi(str_args[i]));
        }
        catch (std::invalid_argument) {
            col_index = get_col_pos(filename, str_args[i]);
            if (col_index == -1)
                throw std::runtime_error("Could not find a column named " + str_args[i]);
            else
                columns.push_back(col_index);
        }
    }

    CSVReader reader(filename, GUESS_CSV, columns);
    vector<string> row;

    if (stdout_) {
        while (reader.read_row(row)) {
            for (size_t i = 0; i < row.size(); i++) {
                std::cout << csv_escape(row[i]);
                if (i + 1 != row.size())
                    std::cout << ",";
            }

            std::cout << "\r\n";
        }
    }
    else {
        CSVWriter writer(outfile);
        writer.write_row(reader.get_col_names());
        while (reader.read_row(row))
            writer.write_row(row);

        writer.close();
    }

    return 0;
}

int cli_sql(deque<string> str_args) {
    string csv_file = str_args.at(0);
    string db_file;

    if (str_args.size() > 1) {
        db_file = str_args.at(1);
    }
    else {
        db_file = helpers::get_filename_from_path(csv_file) + ".sqlite";
        std::cout << "Outputting database to " << db_file << std::endl;
    }

    extra::csv_to_sql(csv_file, db_file);
    return 0;
}

int cli_query(deque<string> str_args) {
    string db_name = str_args.at(0);
    string query = str_args.at(1);
    extra::sql_query(db_name, query);
    return 0;
}

int cli_join(deque<string> str_args) {
    string file1 = str_args.at(0), file2 = str_args.at(1);
    string outfile = str_args.at(2);
    string column1(""), column2("");

    if (str_args.size() >= 4)
        column1 = str_args.at(3);
    if (str_args.size() >= 5)
        column2 = str_args.at(4);

    extra::csv_join(file1, file2, outfile, column1, column2);
    return 0;
}