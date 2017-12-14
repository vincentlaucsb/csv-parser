#define THROW_SQLITE_ERROR \
throw std::runtime_error("[SQLite Error] " + std::string(error_message))

#include "sqlite3.h"
#include "csv_parser.h"
#include <stdio.h>

using std::vector;
using std::string;
using std::map;

namespace csv_parser {
    inline void _throw_on_error(int result, char * error_message=nullptr) {
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

    std::vector<std::string> path_split(std::string path) {
        vector<string> splitted;
        string current_part;
        
        for (size_t i = 0; i < path.size(); i++) {
            switch (path[i]) {
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
        path = path_split(path).back();
        return path.substr(0, path.size() - 3);
    }
    
    std::string sql_sanitize(std::string col_name) {
        /** Sanitize column names for SQL
         *   - Remove bad characters
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

    vector<string> sqlite_types(std::string filename, int nrows) {
        /** Return the preferred data type for the columns of a file */
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
    
    std::vector<std::string> sql_sanitize(std::vector<std::string> col_names) {
        vector<string> new_vec;
        for (auto it = col_names.begin(); it != col_names.end(); ++it)
            new_vec.push_back(sql_sanitize(*it));
        return new_vec;
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
        /** Generate an INSERT VALUES statement */
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

    int csv_to_sql(std::string csv_file, std::string db, std::string table) {
        CSVReader infile(csv_file);

        // Default file name is CSV file minus extension
        if (table == "")
            table = get_filename_from_path(csv_file);
        table = sql_sanitize(table);

        string create_stmt = create_table(csv_file, table);
        string insert_stmt = insert_values(csv_file, table);
        char* error_message = new char[200];
        sqlite3* db_handle;
        const char * unused;
        sqlite3_stmt* insert_stmt_handle;

        if (sqlite3_open(db.c_str(), &db_handle))
            throw std::runtime_error("Failed to open database");

        // 0's are callback function and argument to callback respectively
        if (sqlite3_exec(db_handle, (const char*)create_stmt.c_str(), 0, 0, &error_message) != 0)
            THROW_SQLITE_ERROR;
        
        /* Prepare a statement for inserting values
         * sqlite3_prepare_v2() is the preferred routine
         */
        _throw_on_error(sqlite3_prepare_v2(
            db_handle,                        /* Database handle */
            (const char*)insert_stmt.c_str(), /* SQL statement, UTF-8 encoded */
            -1,                               /* Maximum length of zSql in bytes. */
            &insert_stmt_handle,              /* OUT: Statement handle */
            &unused                           /* OUT: Pointer to unused portion of zSql */
        ));

        if (sqlite3_exec(db_handle, "BEGIN TRANSACTION", NULL, NULL, &error_message) != 0)
            THROW_SQLITE_ERROR;

        vector<void*> row = {};
        vector<int> dtypes;
        string* str_ptr;
        int* int_ptr;
        double* dbl_ptr;

        while (infile.read_row(row, dtypes)) {
            for (size_t i = 0; i < row.size(); i++) {
                switch (dtypes[i]) {
                case 0: // Empty String
                case 1: // String
                    str_ptr = (string*)(row[i]);
                    _throw_on_error(sqlite3_bind_text(
                        insert_stmt_handle, // Pointer to prepared statement
                        i + 1,              // Index of parameter to set
                        str_ptr->c_str(),   // Value to bind
                        str_ptr->size(),    // Size of string in bytes
                        SQLITE_TRANSIENT)); // String destructor
                    delete str_ptr;
                    break;
                case 2:
                    int_ptr = (int*)(row[i]);
                    _throw_on_error(sqlite3_bind_int64(insert_stmt_handle, i + 1, *int_ptr));
                    delete int_ptr;
                    break;
                case 3:
                    dbl_ptr = (double*)(row[i]);
                    _throw_on_error(sqlite3_bind_double(insert_stmt_handle, i + 1, *dbl_ptr));
                    delete dbl_ptr;
                    break;
                }
            }

            _throw_on_error(sqlite3_step(insert_stmt_handle));
            _throw_on_error(sqlite3_reset(insert_stmt_handle));  // Reset
        }

        if (sqlite3_exec(db_handle, "COMMIT TRANSACTION", NULL, NULL, &error_message) != 0)
            THROW_SQLITE_ERROR;
        
        _throw_on_error(sqlite3_finalize(insert_stmt_handle));
        _throw_on_error(sqlite3_close(db_handle));

        return 0;
    }

    void csv_join(std::string filename1, std::string filename2, std::string outfile,
        std::string column1, std::string column2) {

        // Sanitize Names
        std::string table1 = sql_sanitize(get_filename_from_path(filename1));
        std::string table2 = sql_sanitize(get_filename_from_path(filename2));
        column1 = sql_sanitize(column1);
        column2 = sql_sanitize(column2);

        // Create SQLite Database
        std::string db = "temp.sqlite";
        csv_to_sql(filename1, db);
        csv_to_sql(filename2, db);

        sqlite3* db_handle;
        sqlite3_stmt* stmt_handle;
        char* error_message;
        const char * unused;

        // Open database connection and CSV file
        int db_success = sqlite3_open(db.c_str(), &db_handle);
        std::ofstream outfile_writer(outfile, std::ios_base::binary);

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

        sqlite3_prepare_v2(
            db_handle,                   /* Database handle */
            join_statement,              /* SQL statement, UTF-8 encoded */
            -1,                          /* Maximum length of zSql in bytes. */
            &stmt_handle,                /* OUT: Statement handle */
            &unused                      /* OUT: Pointer to unused portion of zSql */
        );

        bool write_col_names = true;
        int col_size = -1;
        string temp;

        while (true) {
            /* 100 --> More rows are available
             * 101 --> Done
             */
            if (sqlite3_step(stmt_handle) == 101)
                break;

            if (col_size < 0)
                col_size = sqlite3_column_count(stmt_handle);
            else if (col_size == 0)
                break;  // No results

            // Write column names
            if (write_col_names && col_size > 0) {
                for (int i = 0; i < col_size; i++) {
                    outfile_writer << sqlite3_column_name(stmt_handle, i);
                    if (i + 1 != col_size)
                        outfile_writer << ",";
                }

                outfile_writer << "\r\n";
                write_col_names = false;
            }

            // Write a row
            for (int i = 0; i < col_size; i++) {
                temp = std::string((char *)sqlite3_column_text(stmt_handle, i));
                outfile_writer << csv_escape(temp);
                
                if (i + 1 != col_size)
                    outfile_writer << ",";
            }

            outfile_writer << "\r\n";
        }

        sqlite3_finalize(stmt_handle);
        sqlite3_close(db_handle);
        remove("temp.sqlite");
    }
}