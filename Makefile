BUILD_DIR = build
TEST_DIR = tests
IDIR = src/
CFLAGS = -pthread -std=c++11
TFLAGS = -I$(IDIR) -Itests/ $(CFLAGS) -Og -g --coverage

# Main Library
SOURCES = $(wildcard src/*.cpp)
OBJECTS = $(subst .cpp,.o,$(subst src/,$(BUILD_DIR)/,$(SOURCES)))

TEST_SOURCES = $(wildcard tests/*.cpp)
TEST_SOURCES_NO_EXT = $(subst tests/,,$(subst .cpp,,$(TEST_SOURCES)))

all: csv_parser test_all clean distclean

# Main Library
csv_parser:
	$(CXX) -c -O3 -Wall $(CFLAGS) $(SOURCES) -I$(IDIR)
	mkdir -p $(BUILD_DIR)
	mv *.o $(BUILD_DIR)
	
test_all:
	make run_test_csv_parser
	make code_cov
	
test_csv_parser:
	$(CXX) -o test_csv_parser $(SOURCES) $(TEST_SOURCES) $(TFLAGS)
	
run_test_csv_parser: test_csv_parser
	mkdir -p tests/temp
	./test_csv_parser
	
	# Test Clean-Up
	rm -rf $(TEST_DIR)/temp
	
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