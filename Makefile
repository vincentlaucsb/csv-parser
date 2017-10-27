IDIR = src/
CFLAGS = -std=c++11 -g
TFLAGS = $(CFLAGS) --coverage

TEST_DIR = tests/

all: csv_parser test_data_type test_read_csv test_csv_stat test_csv_clean  code_cov clean distclean

# Main Library
csv_parser:
	$(CXX) $(IDIR)csv_parser.h -o csv_parser -I$(IDIR) $(CFLAGS)

# Unit Tests
test_data_type:
	$(CXX) $(TEST_DIR)test_data_type.cpp -o test_data_type -I$(IDIR) $(TFLAGS)
	./test_data_type
	rm -f test_data_type
    
test_read_csv:
	$(CXX) $(TEST_DIR)test_read_csv.cpp -o test_read_csv -I$(IDIR) $(TFLAGS)
	./test_read_csv
	rm -f test_read_csv
	
test_csv_stat:
	$(CXX) $(TEST_DIR)test_csv_stat.cpp -o test_csv_stat -I$(IDIR) $(TFLAGS)
	./test_csv_stat
	rm -f test_csv_stat
	
test_csv_clean:
	$(CXX) $(TEST_DIR)test_csv_clean.cpp -o test_csv_clean -I$(IDIR) $(TFLAGS)
	./test_csv_clean
	rm -f test_csv_clean
	rm -f tests/data/fake_data/ints2.csv
	rm -f tests/data/fake_data/ints_skipline2.csv
	rm -f tests/data/real_data/2016_Gaz_place_national.csv
	
.PHONY: all clean distclean

code_cov:
	# Analyze
	lcov --directory $(PWD) --capture --output-file $(PWD)/app.info
	
	# Generate HTML
	genhtml --output-directory $(PWD)/cov_http $(PWD)/app.info

clean:
	# Clean Up
	rm -f csv_parser
	
	# Analyze code coverage data
	bash ./code_cov.sh
	
distclean: clean