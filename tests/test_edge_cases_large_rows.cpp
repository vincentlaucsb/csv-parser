//
// Tests for edge cases with large CSV rows
// Issue #218: Infinite loop when a row exceeds ITERATION_CHUNK_SIZE
//

#include <catch2/catch_all.hpp>
#include <sstream>
#include "csv.hpp"

using namespace csv;

/**
 * Generate a CSV row string of specified size
 * Format: field1,field2,...,fieldN where each field is padded with 'X' to reach target size
 */
std::string generate_large_row(size_t target_bytes, int num_fields = 10) {
    std::string row;
    size_t bytes_per_field = target_bytes / num_fields;
    
    for (int i = 0; i < num_fields; ++i) {
        if (i > 0) row += ",";
        row += "field" + std::to_string(i) + "_";
        // Pad with X's to reach bytes_per_field
        size_t padding = bytes_per_field - row.length() % (bytes_per_field + 1);
        row += std::string(padding, 'X');
    }
    row += "\n";
    return row;
}

TEST_CASE("Edge case: CSV rows larger than default chunk size", "[edge_cases_large_rows]") {
    
    SECTION("Normal row (smaller than default chunk)") {
        // Default chunk size is 10MB, this row is just 1KB
        auto csv_data = "A,B,C\n" 
                       "1,2,3\n"
                       "4,5,6\n"_csv;
        
        int row_count = 0;
        for (auto& row : csv_data) {
            row_count++;
        }
        REQUIRE(row_count == 2);
    }
    
    SECTION("Exception thrown for row exceeding default chunk size (without custom size)") {
        // Create a CSV with one row that exceeds 10MB
        // We'll use a modest 15MB row for testing (not 10GB, to keep test fast)
        std::stringstream ss;
        ss << "Col1,Col2,Col3,Col4,Col5\n";  // Header
        
        // Add a row that's roughly 15MB - exceeds default 10MB chunk
        size_t row_size = 15 * 1024 * 1024;  // 15MB
        ss << generate_large_row(row_size, 5);
        
        CSVReader reader(ss);
        
        // Attempting to iterate should throw exception about chunk size
        REQUIRE_THROWS_WITH(
            [&reader]() {
                for (auto& row : reader) {
                    (void)row;  // Use row to avoid unused warning
                }
            }(),
            Catch::Matchers::ContainsSubstring("chunk size")
        );
    }
    
    SECTION("Custom chunk size allows parsing larger rows") {
        // Create a CSV with one row that exceeds default 10MB but fits in 20MB
        std::stringstream ss;
        ss << "Col1,Col2,Col3\n";  // Header
        
        // Add a row that's roughly 15MB - exceeds default chunk but fits in custom
        size_t row_size = 15 * 1024 * 1024;  // 15MB
        ss << generate_large_row(row_size, 3);
        ss << "8,9,10\n";  // Another smaller row after
        
        CSVReader reader(ss);
        reader.set_chunk_size(20 * 1024 * 1024);  // Set to 20MB
        
        // Should now successfully parse without exception
        int row_count = 0;
        for (auto& row : reader) {
            row_count++;
            REQUIRE(row.size() == 3);  // Each row should have 3 columns
        }
        REQUIRE(row_count == 2);  // Header + 2 data rows
    }
    
    SECTION("Multiple large rows with custom chunk size") {
        std::stringstream ss;
        ss << "A,B,C\n";  // Header
        
        // Add multiple 12MB rows
        size_t row_size = 12 * 1024 * 1024;
        for (int i = 0; i < 3; ++i) {
            ss << generate_large_row(row_size, 3);
        }
        
        CSVReader reader(ss);
        reader.set_chunk_size(15 * 1024 * 1024);  // 15MB chunks
        
        int row_count = 0;
        for (auto& row : reader) {
            row_count++;
            REQUIRE(row.size() == 3);
        }
        REQUIRE(row_count == 3);
    }
    
    SECTION("Invalid chunk size (less than minimum) throws exception") {
        CSVReader reader("A,B,C\n1,2,3\n"_csv);
        
        // Try to set chunk size less than ITERATION_CHUNK_SIZE (10MB)
        REQUIRE_THROWS_WITH(
            reader.set_chunk_size(1024 * 1024),  // 1MB - too small
            Catch::Matchers::ContainsSubstring("at least")
        );
        
        // Even zero should fail with the new message
        REQUIRE_THROWS_WITH(
            reader.set_chunk_size(0),
            Catch::Matchers::ContainsSubstring("at least")
        );
    }
    
    SECTION("Minimum allowed chunk size (exactly ITERATION_CHUNK_SIZE) works") {
        std::stringstream ss;
        ss << "A,B\n1,2\n";
        
        CSVReader reader(ss);
        // Setting to default minimum should succeed
        reader.set_chunk_size(10 * 1024 * 1024);  // Exactly 10MB minimum
        
        int row_count = 0;
        for (auto& row : reader) { row_count++; }
        REQUIRE(row_count == 1);
    }
    
    SECTION("Custom chunk size persists across reads") {
        std::stringstream ss1, ss2;
        
        // First CSV with large row
        ss1 << "X,Y,Z\n";
        size_t row_size = 12 * 1024 * 1024;
        ss1 << generate_large_row(row_size, 3);
        
        // Second CSV with large row
        ss2 << "P,Q,R\n";
        ss2 << generate_large_row(row_size, 3);
        
        CSVReader reader1(ss1);
        reader1.set_chunk_size(15 * 1024 * 1024);
        
        // Reader 2 should use default chunk size, not inherited
        CSVReader reader2(ss2);
        
        // Reader1 should work (has custom size)
        int count1 = 0;
        for (auto& row : reader1) { count1++; }
        REQUIRE(count1 == 1);
        
        // Reader2 should fail (default chunk size)
        REQUIRE_THROWS(
            [&reader2]() {
                for (auto& row : reader2) { (void)row; }
            }()
        );
    }
}

TEST_CASE("Issue #218 - Infinite read loop detection", "[issue_218]") {
    
    SECTION("Detects when row exceeds chunk size and file doesn't end") {
        // This simulates a CSV with one huge row in the middle
        std::stringstream ss;
        ss << "A,B\n";  // Header
        ss << "1,2\n";  // Small row
        
        // Add a row larger than default chunk
        size_t large_row_size = 12 * 1024 * 1024;
        ss << generate_large_row(large_row_size, 2);
        
        CSVReader reader(ss);
        // Don't set custom chunk size - this should trigger the error
        
        // First row should parse fine (small row)
        auto it = reader.begin();
        REQUIRE(it != reader.end());
        ++it;
        
        // Second row should trigger infinite loop detection
        REQUIRE_THROWS_WITH(
            ++it,
            Catch::Matchers::ContainsSubstring("End of file not reached")
        );
    }
}
