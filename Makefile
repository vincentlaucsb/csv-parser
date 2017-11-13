IDIR = src/
CFLAGS = -Wall -std=c++11
TFLAGS = csv_parser.a -I$(IDIR) $(CFLAGS) -g --coverage

SOURCES = $(wildcard src/*.cpp)
OBJECTS = $(subst .cpp,.o,$(subst src/,,$(SOURCES)))

TEST_DIR = tests/

all: csv_parser test_all clean distclean

# Main Library
csv_parser:
	$(CXX) -c -Og -g -pthread --coverage $(CFLAGS) \
		$(SOURCES) -I$(IDIR)
	ar -cvq csv_parser.a $(OBJECTS)
	
cli: csv_parser
	$(CXX) -o csv_parser -pthread $(TFLAGS)

# Unit Tests
test_all: csv_parser
	# More parallel tasks may fail due to g++ being a memory whore
	$(MAKE) -j2 test_data_type test_read_csv test_csv_stat test_csv_clean

test_data_type:
	$(CXX) -o test_data_type $(TEST_DIR)test_data_type.cpp $(TFLAGS)
	./test_data_type
	rm -f test_data_type
    
test_read_csv:
	$(CXX) -o test_read_csv $(TEST_DIR)test_read_csv.cpp $(TFLAGS)
	./test_read_csv
	rm -f test_read_csv
	rm -f test.ndjson
	
test_csv_stat:
	$(CXX) -o test_csv_stat $(TEST_DIR)test_csv_stat.cpp -pthread $(TFLAGS)
	./test_csv_stat
	rm -f test_csv_stat
	
test_csv_clean:
	$(CXX) -o test_csv_clean $(TEST_DIR)test_csv_clean.cpp -pthread $(TFLAGS)
	./test_csv_clean
	# rm -f test_csv_clean
	rm -f tests/data/fake_data/ints2.csv
	rm -f tests/data/fake_data/ints_skipline2.csv
	rm -f tests/data/real_data/2016_Gaz_place_national.csv
	
.PHONY: all clean distclean

code_cov:
	# Analyze
	lcov --directory $(PWD) --capture --output-file $(PWD)/app.info
	
	# Generate HTML
	genhtml --output-directory $(PWD)/cov_http $(PWD)/app.info

docs:
	doxygen Doxyfile
	
clean:
	# Clean Up
	rm -f csv_parser.a
	
	# Analyze code coverage data
	bash ./code_cov.sh
	
distclean: clean