IDIR = src/
CC = g++
CFLAGS = -std=c++11

TEST_DIR = tests/

all: data_type test_data_type clean distclean

# Main Library
data_type:
	$(CC) $(IDIR)data_type.hpp -o data_type $(CFLAGS)

# Unit Tests
test_data_type: data_type
	$(CC) $(TEST_DIR)test_data_type.cpp -o test_data_type -I$(IDIR) $(CFLAGS)
    
.PHONY: all clean distclean

clean:
	# Run Tests
	./test_data_type

	# Clean Up
	rm -f data_type
	rm -f test_data_type
	
distclean: clean