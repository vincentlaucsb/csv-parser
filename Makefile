IDIR = src/
CFLAGS = -Wall -std=c++11
TFLAGS = csv_parser.a -I$(IDIR) $(CFLAGS) -g --coverage

TEST_DIR = tests/

all: csv_parser \
	test_data_type test_read_csv test_csv_stat test_csv_clean \
	code_cov clean distclean

# Main Library
csv_parser:
	$(CXX) -c -O3 -g --coverage $(CFLAGS) $(IDIR)csv_reader.cpp $(IDIR)csv_stat.cpp $(IDIR)csv_merge.cpp $(IDIR)data_type.cpp -I$(IDIR)
	ar -cvq csv_parser.a csv_reader.o csv_stat.o csv_merge.o data_type.o
	
cli: csv_parser
	$(CXX) -o csv_parser $(IDIR)main.cpp $(TFLAGS)

# Unit Tests
test_data_type: csv_parser
	$(CXX) -o test_data_type $(TEST_DIR)test_data_type.cpp $(TFLAGS)
	./test_data_type
	rm -f test_data_type
    
test_read_csv: csv_parser
	$(CXX) -o test_read_csv $(TEST_DIR)test_read_csv.cpp $(TFLAGS)
	./test_read_csv
	rm -f test_read_csv
	rm -f test.ndjson
	
test_csv_stat: csv_parser
	$(CXX) -o test_csv_stat $(TEST_DIR)test_csv_stat.cpp $(TFLAGS)
	./test_csv_stat
	rm -f test_csv_stat
	
test_csv_clean: csv_parser
	$(CXX) -o test_csv_clean $(TEST_DIR)test_csv_clean.cpp $(TFLAGS)
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

clean:
	# Clean Up
	rm -f csv_parser.a
	
	# Analyze code coverage data
	bash ./code_cov.sh
	
distclean: clean