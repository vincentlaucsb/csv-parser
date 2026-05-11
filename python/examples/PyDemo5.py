from pathlib import Path
import csvpy

path = Path(__file__).parent.parent / 'data' / '2015_StateDepartment.csv'
with path.open(newline='', encoding='utf-8') as handle:
    reader = csvpy.reader(handle)
    # Do stuff with rows here
    for row in reader:
        print(row[1])
