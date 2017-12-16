#define THROW_SQLITE_ERROR \
throw std::runtime_error("[SQLite Error] " + std::string(error_message))

#define PRINT_SQLITE_ERROR(a) \
throw std::runtime_error("[SQLite Error] " + std::string(a))

#include "sqlite3.h"
#include "csv_parser.h"
#include "print.h"
#include <stdio.h>

using std::vector;
using std::string;

namespace csv_parser {
    /** @file */
    namespace helpers {
        std::vector<std::string> path_split(std::string path) {
            /** Split a file path into a string vector */

            vector<string> splitted;
            string current_part;

            for (size_t i = 0; i < path.size(); i++) {
                switch (path[i]) {
                case '\\': // Muh Windows
                case '/':
                    splitted.push_back(current_part);
                    current_part.clear();
                    break;
                default:
                    current_part += path[i];
                }
            }

            // Push remainder
            splitted.push_back(current_part);
            return splitted;
        }

        std::string get_filename_from_path(std::string path) {
            /** Given a path containing /'s, extract the filename without extensions */
            path = helpers::path_split(path).back();
            return path.substr(0, path.size() - 3);
        }
    }

    namespace sql {
        void explain_sqlite_error(int error_code) {
            /** https://sqlite.org/rescode.html */

            switch (error_code) {
            // No error
            case 0:
            case 100:
            case 101:
                break;
            case 7:
                PRINT_SQLITE_ERROR("Out of memory");
            case 11:
                PRINT_SQLITE_ERROR("Database has been corrupted");
            case 13:
                PRINT_SQLITE_ERROR("Out of disk space");
            case 14:
                PRINT_SQLITE_ERROR("Could not open file");
            case 25:
                PRINT_SQLITE_ERROR("Value out of range");
            case 26:
                PRINT_SQLITE_ERROR("Not a SQLite database");
            default:
                PRINT_SQLITE_ERROR(std::to_string(error_code));
            }
        }

        /** A connection to a SQLite database */
        class SQLiteConn {
        public:
            SQLiteConn(std::string db_name) {
                /** Open a connection to a SQLite3 database
                 *  @param[in] db_name Path to SQLite3 database
                 */
                if (sqlite3_open(db_name.c_str(), &db))
                    throw std::runtime_error("Failed to open database");
                else
                    db_open = true;
            };

            void exec(std::string query) {
                /** Execute a query that doesn't return anything
                 *  @param[in] query A SQL query
                 */
                if (sqlite3_exec(this->db,
                    (const char*)query.c_str(),
                    0,  // Callback
                    0,  // Arg to callback
                    &error_message))
                    THROW_SQLITE_ERROR;
            }

            void close() {
                if (db_open) { // Prevent double frees
                    sqlite3_close(this->db);
                    db_open = false;
                }
            }

            ~SQLiteConn() {
                /** Close connection when this object gets destroyed */
                this->close();
            }

            sqlite3* db;              /** Raw database handle */
            char * error_message;     /** Buffer for error messages */

        private:
            bool db_open = true;
        };

        // Verbosity levels approaching Java
        /** An interface for executing and iterating through SQL statements */
        class SQLitePreparedStatement {
        public:
            SQLitePreparedStatement(SQLiteConn& conn, std::string& stmt) {
                /** Prepare a SQL statement
                 *  @param[in]  conn An active SQLite connection
                 *  @param[out] stmt A SQL query that should be prepared
                 */

                this->conn = &conn;

                /** Prepare a query for execution */
                int result = sqlite3_prepare_v2(
                    conn.db,                     /* Database handle */
                    (const char *)stmt.c_str(),  /* SQL statement, UTF-8 encoded */
                    stmt.size(),                 /* Maximum length of zSql in bytes. */
                    &(this->statement),          /* OUT: Statement handle */
                    &(this->unused)              /* OUT: Pointer to unused portion of zSql */
                );

                explain_sqlite_error(result);
            }

            void bind(size_t& i, std::string& value,
                sqlite3_destructor_type dest=SQLITE_TRANSIENT) {
                /** Bind text values to the statement
                 *
                 *  **Note:** This function is zero-indexed while
                 *  sqlite3_bind_* is 1-indexed
                 */
                sqlite3_bind_text(
                    this->statement,    // Pointer to prepared statement
                    i + 1,              // Index of parameter to set
                    value.c_str(),      // Value to bind
                    value.size(),       // Size of string in bytes
                    dest);              // String destructor
            }

            template <typename T>
            void bind_int(size_t& i, T& value) {
                /** Bind integer values to the statement */
                sqlite3_bind_int64(this->statement, i + 1, value);
            }

            template <typename T>
            void bind_double(size_t& i, T& value) {
                /** Bind floating point values to the statement */
                sqlite3_bind_double(this->statement, i + 1, value);
            }

            ~SQLitePreparedStatement() {
                /** Call sqlite3_finalize() when this object goes out of scope */
                sqlite3_finalize(this->statement);
            }

            void next() {
                /** Call after bind()-ing values to execute statement */
                if (sqlite3_step(this->statement) != 101 || sqlite3_reset(this->statement) != 0)
                    throw std::runtime_error("Error executing prepared statement.");
            }

        protected:
            SQLiteConn* conn;
            sqlite3_stmt* statement;
            const char * unused;
        };

        /** Used for prepared statements that result queries we want to read */
        class SQLiteResultSet : SQLitePreparedStatement {
        public:
            std::vector<std::string> get_col_names() {
                /** Retrieve the column names of a SQL query result */
                std::vector<std::string> ret;
                int col_size = this->num_cols();
                for (int i = 0; i < col_size; i++)
                    ret.push_back(sqlite3_column_name(this->statement, i));

                return ret;
            }

            std::vector<std::string> get_row() {
                /** After calling next_result(), use this to type-cast
                 *  the next row from a query into a string vector
                 */
                std::vector<std::string> ret;
                int col_size = this->num_cols();

                for (int i = 0; i < col_size; i++) {
                    ret.push_back(std::string((char *)
                        sqlite3_column_text(this->statement, i)));
                }

                return ret;
            }

            int num_cols() {
                /** Returns the number of columns in a SQL query result */
                return sqlite3_column_count(this->statement);
            }

            bool next_result() {
                /** Retrieves the next row from the a SQL result set,
                 *  or returns False if we're done
                 */

                /* 100 --> More rows are available
                 * 101 --> Done
                 */
                return (sqlite3_step(this->statement) == 100);
            }

            using SQLitePreparedStatement::SQLitePreparedStatement;
        private:
        };

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
                new_vec.push_back(sql::sql_sanitize(*it));
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
            vector<string> col_names = sql::sql_sanitize(temp.get_col_names());
            vector<string> col_types = sql::sqlite_types(filename);

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

            sql::SQLiteConn db(db_name);
            std::string create_query = sql::create_table(csv_file, table);
            std::string insert_query = sql::insert_values(csv_file, table);

            db.exec(create_query);
            sql::SQLitePreparedStatement insert_stmt(db, insert_query);
            db.exec("BEGIN TRANSACTION");

            vector<void*> row = {};
            vector<DataType> dtypes;
            string* str_ptr;
            long long int* int_ptr;
            long double* dbl_ptr;

            while (infile.read_row(row, dtypes)) {
                for (size_t i = 0; i < row.size(); i++) {
                    switch (dtypes[i]) {
                    case _null: // Empty String
                    case _string:
                        str_ptr = (string*)(row[i]);
                        insert_stmt.bind(i, *str_ptr);
                        delete str_ptr;
                        break;
                    case _int:
                        int_ptr = (long long int*)(row[i]);
                        insert_stmt.bind_int(i, *int_ptr);
                        delete int_ptr;
                        break;
                    case _float:
                        dbl_ptr = (long double*)(row[i]);
                        insert_stmt.bind_double(i, *dbl_ptr);
                        delete dbl_ptr;
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
            sql::SQLiteConn db(db_name);
            sqlite3_stmt* stmt_handle;
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
            sql::SQLiteResultSet results(db, join_statement_);
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

        void sql_query(std::string db_name, std::string query) {
            sql::SQLiteConn db(db_name);
            sql::SQLiteResultSet rs(db, query);
            bool add_col_names = true;
            vector<vector<string>> print_rows;

            for (int i = 0; rs.next_result() && i < 100; i++) {
                if (add_col_names) {
                    print_rows.push_back(rs.get_col_names());
                    add_col_names = false;
                }

                print_rows.push_back(rs.get_row());
            }

            helpers::print_table(print_rows);
        }
    }
}