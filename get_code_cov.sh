#!/bin/bash
for filename in `find . | egrep '\.cpp'`; 
do 
  gcov-8 -n -o ./include/internal/CMakeFiles/csv.dir $filename > /dev/null; 
done