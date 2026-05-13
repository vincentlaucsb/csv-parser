from pathlib import Path
import fastpycsv

path = Path(__file__).parent.parent / 'data' / '2015_StateDepartment.csv'
with path.open(newline='', encoding='utf-8') as handle:
    reader = fastpycsv.reader(handle)
    # Do stuff with rows here
    for row in reader:
        print(row[1])
