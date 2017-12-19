#include "csv_parser.h"
#include "print.h"
#include <stdio.h>
#include <set>

using std::vector;
using std::string;

namespace csv_parser {
    /** @file */
    namespace helpers {
        std::vector<std::string> split(
            const std::string& str,
            const std::set<char>& delims) {
            /** Split a string by delimiter */
            vector<string> splitted = { "" };
           
            for (auto it = str.begin(); it != str.end(); ++it) {
                if (delims.find(*it) == delims.end()) {
                    splitted.back() += *it;
                }
                else {
                    // Delimiter
                    splitted.push_back("");
                }
            }

            return splitted;
        }

        std::vector<std::string> path_split(const std::string path) {
            /** Split a file path into a string vector */
            return split(path, {'\\', '/'});
        }

        std::string get_filename_from_path(const std::string path) {
            /** Given a path containing /'s, extract the filename without extensions */
            std::string filename = path_split(path).back();

            // Strip out extension
            return split(filename, {'.'}).front();
        }
    }

    namespace sql {
        std::string sql_sanitize(std::string col_name) {
            /** Sanitize column names for SQL
            *   - Remove bad characters
            *   - Replace spaces with underscore
            *   - Place _ in front of numeric names
            */
            string new_str;

            for (size_t i = 0; i < col_name.size(); i++) {
                switch (col_name[i]) {
                case '-':
                case '\\':
                case ',':
                case '.':
                    break;
                case '/':
                case ' ':
                    new_str += '_';
                    break;
                default:
                    new_str += col_name[i];
                }
            }

            if (isdigit(new_str.front()))
                new_str = "_" + new_str;

            // Lowercase
            std::transform(new_str.begin(), new_str.end(),
                new_str.begin(), ::tolower);

            return new_str;
        }

        std::vector<std::string> sql_sanitize(std::vector<std::string> col_names) {
            vector<string> new_vec;
            for (auto it = col_names.begin(); it != col_names.end(); ++it)
                new_vec.push_back(sql_sanitize(*it));
            return new_vec;
        }

        vector<string> sqlite_types(std::string filename, int nrows) {
            /** Return the preferred data type for the columns of a file
            * @param[in] filename Path to CSV file
            * @param[in] nrows    Number of rows to examine
            */
            CSVStat stat(guess_delim(filename));
            stat.read_csv(filename, nrows, true);
            stat.calc(false, false, true);

            vector<string> sqlite_types;
            auto dtypes = stat.get_dtypes();
            size_t most_common_dtype = 0, max_count = 0;

            // Loop over each column
            for (auto col_it = dtypes.begin(); col_it != dtypes.end(); ++col_it) {
                most_common_dtype = 0;
                max_count = 0;

                // Loop over candidate data types
                for (size_t dtype = 0; dtype <= 3; dtype++) {
                    try {
                        if ((size_t)col_it->at(dtype) > max_count) {
                            max_count = col_it->at(dtype);
                            most_common_dtype = dtype;
                        }
                    }
                    catch (std::out_of_range) {}
                }

                switch (most_common_dtype) {
                case 0:
                case 1:
                    sqlite_types.push_back("string");
                    break;
                case 2:
                    sqlite_types.push_back("integer");
                    break;
                case 3:
                    sqlite_types.push_back("float");
                    break;
                }
            }

            return sqlite_types;
        }

        std::string create_table(std::string filename, std::string table) {
            /** Generate a CREATE TABLE statement */
            CSVReader temp(filename);
            temp.close();
            string sql_stmt = "CREATE TABLE " + table + " (";
            vector<string> col_names = sql_sanitize(temp.get_col_names());
            vector<string> col_types = sqlite_types(filename);

            for (size_t i = 0; i < col_names.size(); i++) {
                sql_stmt += col_names[i] + " " + col_types[i];
                if (i + 1 != col_names.size())
                    sql_stmt += ",";
            }

            sql_stmt += ");";
            return sql_stmt;
        }

        std::string insert_values(std::string filename, std::string table) {
            /** Generate an INSERT VALUES statement with placeholders
            *  in accordance with the SQLite C API
            */

            CSVReader temp(filename);
            temp.close();
            vector<string> col_names = temp.get_col_names();
            string sql_stmt = "INSERT INTO " + table + " VALUES (";

            for (size_t i = 1; i <= col_names.size(); i++) {
                sql_stmt += "?";
                sql_stmt += std::to_string(i);
                if (i + 1 <= col_names.size())
                    sql_stmt += ",";
            }

            sql_stmt += ");";
            return sql_stmt;
        }
    }

    inline void _throw_on_error(int result, char * error_message = nullptr) {
        if (result != 0 && result != 101) {
            if (!error_message) {
                throw std::runtime_error("[SQLite Error] Code " + std::to_string(result));
            }
            else {
                throw std::runtime_error("[SQLite Error " +
                    std::to_string(result) + "] " + std::string(error_message));
            }
        }
    }

    namespace extra {
        void csv_to_sql(std::string csv_file, std::string db_name, std::string table) {
            /** Convert a CSV file into a SQLite3 database
             *  @param[in]  csv_file  Path to CSV file
             *  @param[out] db_name   Path to SQLite database
             *                        (will be created if it doesn't exist)
             *  @param[out] table     Name of the table (default: filename)
             */

            CSVReader infile(csv_file);

            // Default file name is CSV file minus extension
            if (table == "")
                table = helpers::get_filename_from_path(csv_file);
            table = sql::sql_sanitize(table);

            sqlite_api::SQLiteConn db(db_name);
            std::string create_query = sql::create_table(csv_file, table);
            std::string insert_query = sql::insert_values(csv_file, table);

            db.exec(create_query);
            sqlite_api::SQLitePreparedStatement insert_stmt(db, insert_query);
            db.exec("BEGIN TRANSACTION");

            vector<CSVField> row;
            std::string str_value;
            long long int int_value;
            long double dbl_value;

            while (infile.read_row(row)) {
                for (size_t i = 0; i < row.size(); i++) {
                    switch (row[i].dtype) {
                    case _null: // Empty String
                    case _string:
                        str_value = row[i].get_string();
                        insert_stmt.bind(i, str_value);
                        break;
                    case _int:
                        int_value = row[i].get_int();
                        insert_stmt.bind_int(i, int_value);
                        break;
                    case _float:
                        dbl_value = row[i].get_float();
                        insert_stmt.bind_double(i, dbl_value);
                        break;
                    }
                }

                insert_stmt.next();
            }

            db.exec("COMMIT TRANSACTION");
        }

        void csv_join(std::string filename1, std::string filename2, std::string outfile,
            std::string column1, std::string column2) {

            // Sanitize Names
            std::string table1 = sql::sql_sanitize(
                helpers::get_filename_from_path(filename1));
            std::string table2 = sql::sql_sanitize(
                helpers::get_filename_from_path(filename2));
            column1 = sql::sql_sanitize(column1);
            column2 = sql::sql_sanitize(column2);

            // Create SQLite Database
            std::string db_name = "temp.sqlite";
            csv_to_sql(filename1, db_name);
            csv_to_sql(filename2, db_name);

            CSVWriter writer(outfile);
            sqlite_api::SQLiteConn db(db_name);
            const char * unused;

            // Compose Join Statement
            char join_statement[500];
            if (column1.empty() && column2.empty()) {
                // No columns specified --> natural join
                sprintf(join_statement, "SELECT * FROM %s NATURAL JOIN %s;",
                    table1.c_str(), table2.c_str());
            }
            else {
                // One or two columns specified
                if (column2.empty())
                    column2 = column1;
                sprintf(join_statement, "SELECT * FROM %s F1, %s F2 WHERE F1.%s = F2.%s;",
                    table1.c_str(), table2.c_str(), column1.c_str(), column2.c_str());
            }

            std::string join_statement_ = join_statement;
            sqlite_api::SQLiteResultSet results(db, join_statement_);
            bool write_col_names = true;

            while (results.next_result()) {
                // Write column names
                if (write_col_names) {
                    writer.write_row(results.get_col_names());
                    write_col_names = false;
                }

                writer.write_row(results.get_row());
            }

            db.close();
            remove("temp.sqlite");
        }
    }
}