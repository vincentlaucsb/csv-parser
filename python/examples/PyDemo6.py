import csvpy

format = csvpy.Format().delimiter(',')
reader = csvpy.parse(
    'Name, Age\nHussein Sarea, 22\nMoataz Sarea, 21',
    format
)
# reader = csvpy.parse_no_header(
#     'Name, Age\nHussein Sarea, 22\nMoataz Sarea, 21',
# )
for r in reader:
    print(r[1].get_str())
