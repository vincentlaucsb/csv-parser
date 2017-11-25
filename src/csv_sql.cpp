#include "sqlite3.h"
#include "csv_parser.h"
#include <stdio.h>

using std::vector;
using std::string;

namespace csv_parser {
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

    int csv_to_sql(std::string csv_file, std::string db, std::string table) {
        sqlite3* db_handle;
        char* error_message;

        // Default file name is CSV file minus extension
        if (table == "") {
            table = path_split(csv_file).back();
            table = table.substr(0, table.size() - 3);
        }

        table = sql_sanitize(table);

        // Open connection
        int db_success = sqlite3_open(db.c_str(), &db_handle);

        // Read CSV file
        CSVReader infile(guess_delim(csv_file));
        infile.read_csv(csv_file);

        string create_table = "CREATE TABLE ";
        create_table += table;
        create_table += " (";

        vector<string> col_names = sql_sanitize(infile.get_col_names());

        for (size_t i = 0; i < col_names.size(); i++) {
            create_table += col_names[i];
            create_table += " TEXT";
            if (i + 1 != col_names.size())
                create_table += ",";
        }

        create_table += ");";

        int stmt = sqlite3_exec(
            db_handle,
            (const char *)create_table.c_str(),
            0,      // Callback function
            0,      // Arg to callback
            &error_message
        );

        if (stmt != 0) {
            std::cerr << "[SQLite Error " << stmt << "] " << error_message << std::endl;
            return stmt;
        }

        /* Prepare a statement
        * sqlite3_prepare_v2() is the preferred routine
        */
        vector<string> record;

        char insert_values_ [200];
        sprintf(insert_values_, "INSERT INTO %s VALUES(", table.c_str());

        string insert_values = std::string(insert_values_);

        sqlite3_stmt* insert_statement;
        const char * unused;
        
        for (size_t i = 1; i <= col_names.size(); i++) {
            insert_values += "?";
            insert_values += std::to_string(i);

            if (i + 1 <= col_names.size())
                insert_values += ",";
        }
        
        insert_values += ");";

        sqlite3_prepare_v2(
            db_handle,                   /* Database handle */
            insert_values.c_str(),       /* SQL statement, UTF-8 encoded */
            -1,                          /* Maximum length of zSql in bytes. */
            &insert_statement,           /* OUT: Statement handle */
            &unused                      /* OUT: Pointer to unused portion of zSql */
        );

        sqlite3_exec(db_handle, "BEGIN TRANSACTION", NULL, NULL, &error_message);

        while (!infile.empty()) {
            record = infile.pop();

            for (size_t i = 0; i < record.size(); i++)
                sqlite3_bind_text(
                    insert_statement,  // Pointer to prepared statement
                    i + 1,             // Index of parameter to set
                    record[i].c_str(), // Value to bind
                    -1,                // Size of string in bytes
                    0                  // String destructor
                );

            sqlite3_step(insert_statement);   // Execute insert
            sqlite3_reset(insert_statement);  // Reset
        }

        sqlite3_exec(db_handle, "COMMIT TRANSACTION", NULL, NULL, &error_message);

        // Free objects
        sqlite3_finalize(insert_statement);
        sqlite3_close(db_handle);
        return 0;
    }

    void csv_join(std::string filename1, std::string filename2, std::string outfile,
        std::string column1, std::string column2) {

        // Sanitize Names
        std::string table1 = sql_sanitize(filename1.substr(
            0, filename1.size() - 4).c_str());
        std::string table2 = sql_sanitize(filename2.substr(
            0, filename2.size() - 4).c_str());
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

        int keep_reading = 0;
        int col_size = -1;
        string temp;

        while (true) {
            keep_reading = sqlite3_step(stmt_handle);

            /* 100 --> More rows are available
             * 101 --> Done
             */
            if (keep_reading == 101)
                break;

            if (col_size < 0)
                col_size = sqlite3_column_count(stmt_handle);
            else if (col_size == 0)
                break;  // No results

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