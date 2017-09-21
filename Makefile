IDIR = src/
CC = g++
CFLAGS = -std=c++11 -g

TEST_DIR = tests/

all: csv_parser data_type test_data_type test_read_csv clean distclean

# Main Library
csv_parser:
	$(CC) $(IDIR)csv_parser.hpp -o csv_parser -I$(IDIR) $(CFLAGS)

data_type:
	$(CC) $(IDIR)data_type.hpp -o data_type $(CFLAGS)

# Unit Tests
test_data_type: data_type
	$(CC) $(TEST_DIR)test_data_type.cpp -o test_data_type -I$(IDIR) $(CFLAGS)
    
test_read_csv: data_type
	$(CC) $(TEST_DIR)test_read_csv.cpp -o test_read_csv -I$(IDIR) $(CFLAGS)
	
.PHONY: all clean distclean

clean:
	# Run Tests
	./test_data_type
	./test_read_csv

	# Clean Up
	rm -f csv_parser
	rm -f data_type
	rm -f test_data_type
	rm -f test_read_csv
	
distclean: clean