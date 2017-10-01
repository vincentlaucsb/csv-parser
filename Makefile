IDIR = src/
CFLAGS = -std=c++11 -g

TEST_DIR = tests/

all: csv_parser data_type test_data_type test_read_csv test_csv_stat test_csv_clean clean distclean

# Main Library
csv_parser:
	$(CXX) $(IDIR)csv_parser.hpp -o csv_parser -I$(IDIR) $(CFLAGS)

data_type:
	$(CXX) $(IDIR)data_type.hpp -o data_type $(CFLAGS)

# Unit Tests
test_data_type: data_type
	$(CXX) $(TEST_DIR)test_data_type.cpp -o test_data_type -I$(IDIR) $(CFLAGS)
    
test_read_csv:
	$(CXX) $(TEST_DIR)test_read_csv.cpp -o test_read_csv -I$(IDIR) $(CFLAGS)
	
test_csv_stat:
	$(CXX) $(TEST_DIR)test_csv_stat.cpp -o test_csv_stat -I$(IDIR) $(CFLAGS)
	
test_csv_clean:
	$(CXX) $(TEST_DIR)test_csv_clean.cpp -o test_csv_clean -I$(IDIR) $(CFLAGS)
	
.PHONY: all clean distclean

clean:
	# Run Tests
	./test_data_type
	./test_read_csv
	./test_csv_stat
	./test_csv_clean

	# Clean Up
	rm -f csv_parser
	rm -f data_type
	rm -f test_data_type
	rm -f test_read_csv
	rm -f test_csv_stat
	rm -f test_csv_clean
	
distclean: clean