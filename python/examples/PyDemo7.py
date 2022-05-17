from pathlib import Path
import csvpy

path = Path(__file__).parent.parent / 'data' / '2015_StateDepartment.csv'

info = csvpy.get_file_info(str(path))

print(info.filename)
print(info.col_names)
print(info.delim)
print(info.n_rows)
print(info.n_cols)