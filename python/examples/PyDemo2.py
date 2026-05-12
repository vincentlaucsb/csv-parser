from pathlib import Path
import fastpycsv

path = Path(__file__).parent.parent / 'data' / '2015_StateDepartment.csv'
reader = fastpycsv.Reader(str(path))

for row in reader:
    row['Year'].get_int()
    # row[0].get_int()
