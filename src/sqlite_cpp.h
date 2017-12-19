#define THROW_SQLITE_ERROR \
throw std::runtime_error("[SQLite Error] " + std::string(error_message))

#define PRINT_SQLITE_ERROR(a) \
throw std::runtime_error("[SQLite Error] " + std::string(a))

#include "sqlite3.h"
#include <vector>
#include <string>
#include <memory>

namespace sqlite_api {
    void explain_sqlite_error(int error_code);

    /** Wrapper over a sqlite3 pointer */
    class _SQLiteDb {
    public:
        _SQLiteDb() {};
        ~_SQLiteDb() {
            sqlite3_close(db);
        }
        sqlite3* db;
    };

    /** Connection to a SQLite database */
    class SQLiteConn {
    public:
        SQLiteConn(std::string db_name);
        void exec(std::string query);
        void close();

        sqlite3* get_ptr();
        std::shared_ptr<_SQLiteDb> db =
            std::make_shared<_SQLiteDb>(); /** Database handle */
        char * error_message;              /** Buffer for error messages */
    };

    /** Wrapper over a sqlite3_stmt pointer */
    class _SQLiteStatement {
    public:
        _SQLiteStatement() {};
        ~_SQLiteStatement() {
            sqlite3_finalize(this->stmt);
        }
        sqlite3_stmt* stmt;
    };

    // Verbosity levels approaching Java
    /** An interface for executing and iterating through SQL statements */
    class SQLitePreparedStatement {
    public:
        SQLitePreparedStatement() {};
        SQLitePreparedStatement(SQLiteConn& conn);
        SQLitePreparedStatement(SQLiteConn& conn, const std::string& stmt);
        ~SQLitePreparedStatement();

        void prepare(const std::string& stmt);
        void bind(size_t& i, std::string& value,
            sqlite3_destructor_type dest = SQLITE_TRANSIENT);

        template <typename T>
        inline void bind_int(size_t& i, T& value) {
            /** Bind integer values to the statement */
            sqlite3_bind_int64(this->get_ptr(), i + 1, value);
        }

        template <typename T>
        inline void bind_double(size_t& i, T& value) {
            /** Bind floating point values to the statement */
            sqlite3_bind_double(this->get_ptr(), i + 1, value);
        }

        void next();
        void close();

    protected:
        sqlite3_stmt* get_ptr();
        SQLiteConn* conn;
        std::shared_ptr<_SQLiteStatement> stmt = std::make_shared<_SQLiteStatement>();
        const char * unused;
    };

    class SQLiteResultSet : SQLitePreparedStatement {
    public:
        std::vector<std::string> get_col_names();
        std::vector<std::string> get_row();
        int num_cols();
        bool next_result();
        using SQLitePreparedStatement::prepare;
        using SQLitePreparedStatement::close;
        using SQLitePreparedStatement::SQLitePreparedStatement;
    };

    SQLiteResultSet sql_query(std::string db_name, std::string query);   
}