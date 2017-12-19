#include "sqlite_cpp.h"

namespace sqlite_api {
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

    SQLiteConn::SQLiteConn(std::string db_name) {
        /** Open a connection to a SQLite3 database
        *  @param[in] db_name Path to SQLite3 database
        */
        if (sqlite3_open(db_name.c_str(), &(this->db.get()->db)))
            throw std::runtime_error("Failed to open database");
    };

    void SQLiteConn::exec(std::string query) {
        /** Execute a query that doesn't return anything
        *  @param[in] query A SQL query
        */
        if (sqlite3_exec(this->get_ptr(),
            (const char*)query.c_str(),
            0,  // Callback
            0,  // Arg to callback
            &error_message))
            THROW_SQLITE_ERROR;
    }

    sqlite3* SQLiteConn::get_ptr() {
        return this->db.get()->db;
    }

    void SQLiteConn::close() {
        sqlite3_close(this->get_ptr());
    }

    //
    // SQLitePreparedStatement
    //

    SQLitePreparedStatement::SQLitePreparedStatement(SQLiteConn& conn) {
        this->conn = &conn;
    }

    SQLitePreparedStatement::SQLitePreparedStatement(
        SQLiteConn& conn, const std::string& stmt) {
        /** Prepare a SQL statement
        *  @param[in]  conn An active SQLite connection
        *  @param[out] stmt A SQL query that should be prepared
        */

        this->conn = &conn;
        this->prepare(stmt);
    }


    sqlite3_stmt* SQLitePreparedStatement::get_ptr() {
        return this->stmt.get()->stmt;
    }
    
    void SQLitePreparedStatement::prepare(const std::string& stmt) {
        /** Prepare a query for execution */
        int result = sqlite3_prepare_v2(
            this->conn->get_ptr(),       /* Database handle */
            (const char *)stmt.c_str(),  /* SQL statement, UTF-8 encoded */
            stmt.size(),                 /* Maximum length of zSql in bytes. */
            &(this->stmt.get()->stmt),   /* OUT: Statement handle */
            &(this->unused)              /* OUT: Pointer to unused portion of zSql */
        );

        explain_sqlite_error(result);
    }


    void SQLitePreparedStatement::close() {
        // TO DO: Fix potential double free errors
        sqlite3_finalize(this->get_ptr());
    }

    SQLitePreparedStatement::~SQLitePreparedStatement() {
        /** Call sqlite3_finalize() when this object goes out of scope */
        this->close();
    }

    void SQLitePreparedStatement::bind(
        size_t& i,
        std::string& value,
        sqlite3_destructor_type dest) {
        /** Bind text values to the statement
        *
        *  **Note:** This function is zero-indexed while
        *  sqlite3_bind_* is 1-indexed
        */
        sqlite3_bind_text(
            this->get_ptr(),    // Pointer to prepared statement
            i + 1,              // Index of parameter to set
            value.c_str(),      // Value to bind
            value.size(),       // Size of string in bytes
            dest);              // String destructor
    }

    void SQLitePreparedStatement::next() {
        /** Call after bind()-ing values to execute statement */
        if (sqlite3_step(this->get_ptr()) != 101 || sqlite3_reset(this->get_ptr()) != 0)
            throw std::runtime_error("Error executing prepared statement.");
    }

    //
    // SQLiteResultSet
    // 

    /** Used for prepared statements that result queries we want to read */
    std::vector<std::string> SQLiteResultSet::get_col_names() {
        /** Retrieve the column names of a SQL query result */
        std::vector<std::string> ret;
        int col_size = this->num_cols();
        for (int i = 0; i < col_size; i++)
            ret.push_back(sqlite3_column_name(this->get_ptr(), i));

        return ret;
    }

    std::vector<std::string> SQLiteResultSet::get_row() {
        /** After calling next_result(), use this to type-cast
        *  the next row from a query into a string vector
        */
        std::vector<std::string> ret;
        int col_size = this->num_cols();

        for (int i = 0; i < col_size; i++) {
            ret.push_back(std::string((char *)
                sqlite3_column_text(this->get_ptr(), i)));
        }

        return ret;
    }

    int SQLiteResultSet::num_cols() {
        /** Returns the number of columns in a SQL query result */
        return sqlite3_column_count(this->get_ptr());
    }

    bool SQLiteResultSet::next_result() {
        /** Retrieves the next row from the a SQL result set,
        *  or returns False if we're done
        */

        /* 100 --> More rows are available
        * 101 --> Done
        */
        return (sqlite3_step(this->get_ptr()) == 100);
    }

    SQLiteResultSet sql_query(std::string db_name, std::string query) {
        /** Given a query return a SQLiteResultSet */

        sqlite_api::SQLiteConn db(db_name);
        sqlite_api::SQLiteResultSet rs(db, query);
        return rs;
    }
}