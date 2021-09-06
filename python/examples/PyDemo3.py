from pathlib import Path
import csvpy

path = Path(__file__).parent.parent / 'data' / '2015_StateDepartment.csv'
reader = csvpy.Reader(str(path))

for row in reader:
    if row['Year'].is_int():
        row['Year'].get_int()
    elif row['Year'].is_float():
        row['Year'].get_float()
    elif row['Year'].is_str():
        row['Year'].get_str()
    elif row['Year'].is_null():
        pass
