from pathlib import Path
import csvpy

path = Path(__file__).parent.parent / 'data' / '2015_StateDepartment.csv'
format = csvpy.Format()
format.delimiter(',').quote('"').header_row(2)
reader = csvpy.Reader(str(path), format)
for row in reader:
    # Do stuff with rows here
    print(row[1].get_str())
