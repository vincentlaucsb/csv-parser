IDIR = src/
SQLITE3 = src/sqlite3/
CFLAGS = -ldl -pthread -std=c++11
TFLAGS = -I$(IDIR) -Itests/ $(CFLAGS) -Og -g --coverage

# Main Library
SOURCES = $(wildcard src/*.cpp)
OBJECTS_ = $(subst .cpp,.o,$(subst src/,,$(SOURCES)))
OBJECTS = $(subst main.o,,$(OBJECTS_))

TEST_SOURCES = $(wildcard tests/*.cpp)
TEST_OBJECTS = $(subst .cpp,.o,$(subst tests/,,$(TEST_SOURCES)))

TEST_DIR = tests/

all: csv_parser test_all clean distclean

# Main Library
csv_parser:
	$(CC) -c -O3 $(SQLITE3)sqlite3.c -pthread -ldl -I$(SQLITE3)
	$(CXX) -c -O3 -Wall $(CFLAGS) $(SOURCES) -I$(IDIR)
	ar -cvq csv_parser.a $(OBJECTS) sqlite3.o
	
cli: csv_parser
	$(CXX) -o csv_parser csv_parser.a src/main.cpp -O3 -Wall $(CFLAGS)

# Unit Tests
test_all:
	$(CC) -c -Og $(SQLITE3)sqlite3.c -pthread -ldl -I$(SQLITE3)
	$(CXX) -c $(TFLAGS) $(SOURCES) -I$(IDIR)	
	rm -f main.o

	$(CXX) -o test_csv_parser sqlite3.o $(OBJECTS) $(TEST_SOURCES) $(TFLAGS) -I$(SQLITE3)
	./test_csv_parser
	
	# read_csv
	rm -f test_read_csv
	rm -f test.ndjson
	
	# csv_clean
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