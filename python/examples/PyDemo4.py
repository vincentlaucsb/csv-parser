from pathlib import Path
import csvpy

path = Path(__file__).parent.parent / 'data' / '2015_StateDepartment.csv'
reader = csvpy.Reader(str(path))

# for row in reader:
#     print(row.to_json())
#     print(row.to_json_array())

for row in reader:
    # You can pass in a list of column names to slice or rearrange the outputted JSON
    print(row.to_json(['Entity Type', 'Year']))
    print(row.to_json_array(['Year', 'Entity Type']))