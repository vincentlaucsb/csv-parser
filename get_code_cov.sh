#!/bin/bash
for filename in `find . | egrep '\.cpp'`; 
do 
  gcov-8 -n -o . $filename > /dev/null; 
done