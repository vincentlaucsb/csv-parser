import argparse
import csv

parser = argparse.ArgumentParser(description='Count the number of lines in a CSV')
parser.add_argument('file', type=str, nargs=1,
                    help='File to parse')
parser.add_argument('encoding', nargs='?', type=str, default='utf-8',
                    help='File encoding')

args = parser.parse_args()
file = args.file[0]
enc = args.encoding

j = 0
with open(file, 'r', encoding=enc) as csv_file:
    reader = csv.reader(csv_file)
    for i in reader:
        j += 1
        
print(j)