from io import StringIO

import fastpycsv

reader = fastpycsv.reader(StringIO('Name, Age\nHussein Sarea, 22\nMoataz Sarea, 21'))
for row in reader:
    print(row[1])
