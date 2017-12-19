BUILD_DIR = build
IDIR = src/
SQLITE_CPP = lib/sqlite-cpp
SQLITE3 = $(SQLITE_CPP)/lib
CFLAGS = -ldl -pthread -std=c++11
TFLAGS = -I$(IDIR) -Itests/ $(CFLAGS) -Og -g --coverage

# Main Library
SOURCES = $(subst src/main.cpp,,$(wildcard src/*.cpp))
OBJECTS = $(subst .cpp,.o,$(subst src/,$(BUILD_DIR)/,$(SOURCES)))

TEST_SOURCES = $(wildcard tests/*.cpp)
TEST_SOURCES_NO_EXT = $(subst tests/,,$(subst .cpp,,$(TEST_SOURCES)))
TEST_DIR = tests

all: csv_parser test_all clean distclean

# SQLite3
lib/sqlite3.o:
	$(CC) -c -o lib/sqlite3.o -O3 $(SQLITE3)/sqlite3.c -pthread -ldl -I$(SQLITE3)/
	
lib/sqlite_cpp.o: lib/sqlite3.o
	$(CXX) -c -o lib/sqlite_cpp.o -O3 $(SQLITE_CPP)/src/sqlite_cpp.cpp -pthread -ldl -I$(SQLITE_CPP)/src/

# Main Libraryc
csv_parser: lib/sqlite_cpp.o
	$(CXX) -c -O3 -Wall $(CFLAGS) $(SOURCES) -I$(IDIR)
	mkdir -p $(BUILD_DIR)
	mv *.o $(BUILD_DIR)
	
cli: csv_parser lib/sqlite3.o
	$(CXX) -o csv_parser $(OBJECTS) lib/sqlite3.o src/main.cpp -O3 -Wall $(CFLAGS)
	mv csv_parser $(PWD)/bin
	alias csv_parser='./$(PWD)/bin/csv_parser'

test_all:
	make run_test_csv_parser
	#make test_cli
	make code_cov
	
test_csv_parser: lib/sqlite_cpp.o
	$(CXX) -o test_csv_parser lib/sqlite3.o lib/sqlite_cpp.o $(SOURCES) $(TEST_SOURCES) $(TFLAGS) -I$(SQLITE3)/ -I$(SQLITE_CPP)/src
	
run_test_csv_parser: test_csv_parser
	mkdir -p tests/temp
	./test_csv_parser
	
	# Test Clean-Up
	rm -rf $(TEST_DIR)/temp
	
#test_cli:
	#python3 $(TEST_DIR)/test_cli.py
	
code_cov: test_csv_parser
	mkdir -p test_results
	mv *.gcno *.gcda $(PWD)/test_results
	gcov $(SOURCES) -o test_results --relative-only
	mv *.gcov test_results
	
code_cov_report:
	cd test_results
	lcov --capture --directory test_results --output-file coverage.info
	genhtml coverage.info --output-directory out
	
.PHONY: all clean distclean
	
docs:
	doxygen Doxyfile
	
clean:	
	rm -rf test_csv_parser
	
distclean: clean