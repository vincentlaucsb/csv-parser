BUILD_DIR = 
IDIR = src/
SQLITE3 = lib/sqlite3
CFLAGS = -ldl -pthread -std=c++11
TFLAGS = -I$(IDIR) -Itests/ $(CFLAGS) -Og -g --coverage

# Main Library
SOURCES_ = $(wildcard src/*.cpp)
SOURCES = $(subst src/main.cpp,,$(SOURCES_))
OBJECTS = $(subst .cpp,.o,$(subst src/,$(BUILD_DIR),$(SOURCES)))

TEST_SOURCES = $(wildcard tests/*.cpp)
TEST_OBJECTS = $(subst .cpp,.o,$(subst tests/,,$(TEST_SOURCES)))

TEST_DIR = tests/

all: csv_parser test_all clean distclean

# SQLite3
lib/sqlite3.o:
	unzip lib/sqlite-amalgamation-3210000.zip
	mv sqlite-amalgamation-3210000 $(PWD)/$(SQLITE3)
	$(CC) -c -o $(PWD)/lib/sqlite3.o -O3 $(SQLITE3)/sqlite3.c -pthread -ldl -I$(SQLITE3)/

# Main Library
csv_parser: lib/sqlite3.o
	$(CXX) -c -O3 -Wall $(CFLAGS) $(SOURCES) -I$(IDIR)
	ar -cvq $(BUILD_DIR)/csv_parser.a $(OBJECTS)
	
cli: csv_parser lib/sqlite3.o
	$(CXX) -o csv_parser $(OBJECTS) lib/sqlite3.o src/main.cpp -O3 -Wall $(CFLAGS)

test_all: lib/sqlite3.o
	$(CXX) -o test_csv_parser lib/sqlite3.o $(SOURCES) $(TEST_SOURCES) $(TFLAGS) -I$(SQLITE3)/
	./test_csv_parser
	
	# CLI tests
	cd tests && python test_cli.py
	
	# read_csv
	rm -f test_read_csv
	rm -f test.ndjson
	
	# csv_clean
	rm -f tests/data/fake_data/ints2.csv
	rm -f tests/data/fake_data/ints_skipline2.csv
	rm -f tests/data/real_data/2016_Gaz_place_national.csv
	
	# csv_sql
	rm -f ints.sqlite
	
.PHONY: all clean distclean

docs:
	doxygen Doxyfile
	
clean:
	rm -rf build
	
distclean: clean