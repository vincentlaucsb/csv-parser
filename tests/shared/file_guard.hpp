/** @file
 *  @brief RAII helper for test file cleanup
 *  
 *  Ensures test files are always deleted, even if test assertions fail.
 *  Prevents leftover files from accumulating in the repository.
 */

#pragma once

#include <string>
#include <cstdio>

/** RAII guard for temporary test files
 *  
 *  Usage:
 *  \code
 *  TEST_CASE("My test", "[test]") {
 *      auto filename = "test.csv";
 *      FileGuard cleanup(filename);  // Auto-deletes on scope exit
 *      
 *      std::ofstream out(filename);
 *      // ... write test data ...
 *      
 *      CSVReader reader(filename);
 *      REQUIRE(reader.n_rows() == 100);  // File cleaned up even if this fails
 *  }
 *  \endcode
 */
struct FileGuard {
    std::string filename;
    
    explicit FileGuard(std::string fname) : filename(std::move(fname)) {}
    
    ~FileGuard() {
        std::remove(filename.c_str());
    }
    
    // Prevent copying (ensures one guard per file)
    FileGuard(const FileGuard&) = delete;
    FileGuard& operator=(const FileGuard&) = delete;
    
    // Allow moving
    FileGuard(FileGuard&&) = default;
    FileGuard& operator=(FileGuard&&) = default;
};
