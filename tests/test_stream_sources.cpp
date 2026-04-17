//
// Tests for third-party stream compatibility
// Issue #259: StreamParser compilation error with some stream sources
//

#include <catch2/catch_all.hpp>
#include <sstream>
#include <memory>
#include "csv.hpp"
#include "shared/non_seekable_stream.hpp"

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

    SECTION("Non-seekable stream parses with guessed format") {
        NonSeekableStream stream("A,B,C\n1,2,3\n4,5,6\n");

        // Non-seekable contract: seek/tell must fail.
        stream.seekg(0, std::ios::beg);
        REQUIRE(stream.fail());
        stream.clear();

        REQUIRE(stream.tellg() == std::streampos(-1));
        stream.clear();

        CSVReader reader(stream);

        int row_count = 0;
        for (auto& row : reader) {
            REQUIRE(row.size() == 3);
            REQUIRE(row[0].get<int>() == (row_count * 3 + 1));
            REQUIRE(row[1].get<int>() == (row_count * 3 + 2));
            REQUIRE(row[2].get<int>() == (row_count * 3 + 3));
            row_count++;
        }
        REQUIRE(row_count == 2);
    }

    SECTION("Non-seekable stream parses with custom delimiter") {
        NonSeekableStream stream("X|Y|Z\n10|20|30\n40|50|60\n");
        CSVFormat format;
        format.delimiter('|');

        CSVReader reader(stream, format);

        int row_count = 0;
        for (auto& row : reader) {
            REQUIRE(row.size() == 3);
            REQUIRE(row[0].get<int>() == (row_count == 0 ? 10 : 40));
            REQUIRE(row[1].get<int>() == (row_count == 0 ? 20 : 50));
            REQUIRE(row[2].get<int>() == (row_count == 0 ? 30 : 60));
            row_count++;
        }
        REQUIRE(row_count == 2);
    }
}

TEST_CASE("StringViewStreamBuf seek coverage", "[stream_sources][string_view_stream]") {
    // StringViewStreamBuf::seekoff and seekpos stay uncovered when tests only do
    // sequential reads via parse_unsafe(). These tests exercise every branch of
    // seekoff/seekpos directly so coverage tools can see them.
    using csv::internals::StringViewStreamBuf;

    const std::string data = "ABCDE";
    StringViewStreamBuf buf(data);
    std::istream stream(&buf);

    SECTION("seekoff beg+0 is start") {
        auto pos = stream.seekg(0, std::ios::beg).tellg();
        REQUIRE(pos == std::streampos(0));
        REQUIRE(stream.get() == 'A');
    }

    SECTION("seekoff cur advances correctly") {
        stream.get(); // consume 'A', current = 1
        stream.seekg(2, std::ios::cur);
        REQUIRE(stream.tellg() == std::streampos(3));
        REQUIRE(stream.get() == 'D');
    }

    SECTION("seekoff end reaches last byte") {
        stream.seekg(-1, std::ios::end);
        REQUIRE(stream.tellg() == std::streampos(4));
        REQUIRE(stream.get() == 'E');
    }

    SECTION("seekoff out-of-range returns failure") {
        // Negative from beg
        auto pos = stream.seekg(-1, std::ios::beg).tellg();
        REQUIRE(pos == std::streampos(-1));
        stream.clear();

        // Past end
        pos = stream.seekg(static_cast<std::streamoff>(data.size() + 1), std::ios::beg).tellg();
        REQUIRE(pos == std::streampos(-1));
        stream.clear();
    }

    SECTION("seekoff with write-only openmode returns failure") {
        // which=out — read buffer should refuse
        auto pos = buf.pubseekoff(0, std::ios::beg, std::ios::out);
        REQUIRE(pos == std::streampos(-1));
    }

    SECTION("seekpos delegates to seekoff correctly") {
        stream.seekg(std::streampos(2));
        REQUIRE(stream.tellg() == std::streampos(2));
        REQUIRE(stream.get() == 'C');
    }

    SECTION("parse_unsafe round-trip is consistent with seek-reset") {
        // Verify that after a seek the remaining parse still yields correct data.
        const std::string csv_data = "X,Y\n10,20\n30,40\n";
        csv::internals::StringViewStreamBuf csv_buf(csv_data);
        std::istream csv_stream(&csv_buf);

        // Advance past the header, then rewind to beg before handing to CSVReader
        csv_stream.get(); // consume 'X'
        csv_stream.seekg(0, std::ios::beg);
        REQUIRE(csv_stream.tellg() == std::streampos(0));

        CSVReader reader(csv_stream);
        std::vector<int> xs, ys;
        for (auto& row : reader) {
            xs.push_back(row["X"].get<int>());
            ys.push_back(row["Y"].get<int>());
        }

        REQUIRE(xs == std::vector<int>{10, 30});
        REQUIRE(ys == std::vector<int>{20, 40});
    }
}
