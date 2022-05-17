from pathlib import Path
import csvpy

path = Path(__file__).parent.parent / 'data' / '2015_StateDepartment.csv'
reader = csvpy.Reader(str(path))

for row in reader:
    for field in row:
        # field.get_int()
        # field.get_float()
        # field.get_double()
        # field.get_sv()
        print(field.get_str())

