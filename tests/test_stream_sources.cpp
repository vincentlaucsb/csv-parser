//
// Tests for third-party stream compatibility
// Issue #259: StreamParser compilation error with some stream sources
//

#include <catch2/catch_all.hpp>
#include <sstream>
#include <memory>
#include "csv.hpp"

using namespace csv;

/**
 * Mock stream class that mimics third-party libraries
 * with deleted copy constructors (e.g. boost streams, custom streams)
 */
class NonCopyableStream : public std::istringstream {
public:
    explicit NonCopyableStream(const std::string& data)
        : std::istringstream(data) {}

    // Delete copy constructor and assignment — the point of this mock.
    // Move is intentionally not declared: std::istringstream's move ctor is
    // deleted in some stdlibs (GCC 14), and we never need to move this object.
    NonCopyableStream(const NonCopyableStream&) = delete;
    NonCopyableStream& operator=(const NonCopyableStream&) = delete;
};

TEST_CASE("Third-party stream compatibility", "[stream_sources][issue_259]") {
    
    SECTION("Standard istringstream works") {
        std::istringstream ss("A,B,C\n1,2,3\n4,5,6\n");
        CSVReader reader(ss);
        
        int row_count = 0;
        for (auto& row : reader) {
            REQUIRE(row.size() == 3);
            row_count++;
        }
        REQUIRE(row_count == 2);
    }
    
    SECTION("Non-copyable stream (mimics third-party libraries)") {
        // This stream has a deleted copy constructor
        NonCopyableStream stream("Name,Age,City\nAlice,30,NYC\nBob,25,LA\n");
        
        // Before fix: This would fail compilation with "use of deleted function"
        // After fix: Should work fine because we store stream as reference
        CSVReader reader(stream);
        
        int row_count = 0;
        std::vector<std::string> names;
        for (auto& row : reader) {
            REQUIRE(row.size() == 3);
            names.push_back(row["Name"].get());
            row_count++;
        }
        
        REQUIRE(row_count == 2);
        REQUIRE(names[0] == "Alice");
        REQUIRE(names[1] == "Bob");
    }
    
    SECTION("Non-copyable stream with multiple reads") {
        NonCopyableStream stream("X,Y,Z\n10,20,30\n40,50,60\n70,80,90\n");
        CSVReader reader(stream);
        
        std::vector<int> x_values;
        for (auto& row : reader) {
            x_values.push_back(row["X"].get<int>());
        }
        
        REQUIRE(x_values.size() == 3);
        REQUIRE(x_values[0] == 10);
        REQUIRE(x_values[1] == 40);
        REQUIRE(x_values[2] == 70);
    }
    
    SECTION("Non-copyable stream with custom format") {
        NonCopyableStream stream("Name|Age\nAlice|30\nBob|25\n");
        CSVFormat format;
        format.delimiter('|');
        
        CSVReader reader(stream, format);
        
        int row_count = 0;
        for (auto& row : reader) {
            REQUIRE(row.size() == 2);
            row_count++;
        }
        REQUIRE(row_count == 2);
    }
    
    SECTION("Non-copyable stream passed without explicit copy") {
        // Confirms that CSVReader stores the stream by reference (not by value),
        // so a non-copyable stream can be passed as a named lvalue.
        // Before the fix for issue #259, this would fail to compile because
        // the parser tried to copy the stream object internally.
        NonCopyableStream stream("ID,Value\n1,100\n2,200\n");
        CSVReader reader(stream);

        int row_count = 0;
        for (auto& row : reader) {
            (void)row;
            row_count++;
        }
        REQUIRE(row_count == 2);
    }
}
