import os
import re

CPP_SEP = '/'

''' Represents a file path '''
class Path(list):
    def __init__(self, *args):
        super().__init__()

        if (len(args) > 0 and type(args[0]) is list):
            for p in args[0]:
                self.append(p)
        else:
            for p in args:
                self.append(p)

    def append(self, sub: str):
        separated = sub.split(os.sep)
        if (len(separated) == 1):
            separated = sub.split(CPP_SEP)

        for i in separated:
            if (i == '..'):
                # Go up a path
                self.pop()
            else:
                super().append(i)

    def copy(self):
        temp = Path()
        for i in self:
            temp.append(i)
        return temp

    def join(self, sub: str):
        temp = self.copy()
        temp.append(sub)
        return temp

    ''' Return the first element of the path '''
    def dirname(self) -> str:
        try:
            return self[0]
        except IndexError:
            return ''

    def ext(self) -> str:
        try:
            return self[-1].split('.')[-1]
        except IndexError:
            return ''

    def __str__(self):
        if (len(self) == 1):
            return self[0] + '/'

        return '/'.join(self)

    def __hash__(self):
        return hash(str(self))

def header_list(files: list) -> list:
    '''
    Given a list of files, compute the list of header files in the order in which they should
    be included to avoid conflicts
    '''

    dependencies = {}
    headers = []

    ''' Iterate over every .cpp and .hpp file '''
    for file in files:
        file_ext = file.ext()
        if (file_ext == 'hpp' or file_ext == 'h'):
            dependencies[file] = get_dependencies(file)['local']

    while dependencies:
        for file in list(dependencies.keys()):
            # Remove includes we've already included
            dependencies[file] = [i for i in dependencies[file] if i not in headers]

            # If no more dependencies, add file
            if not dependencies[file]:
                headers.append(file)
                dependencies.pop(file)

    return headers

def get_dependencies(file: Path) -> dict:
    ''' Parse a .cpp/.hpp file for its system and local dependencies '''

    dir = Path(file[:-1])

    headers = {
        "system": [],
        "local": []
    }

    with open(str(file), mode='r') as infile:
        for line in infile:
            sys_include = re.search('#include <(?P<file>.*)>', line)
            local_include = re.search('#include "(?P<file>.*)"', line)
            if sys_include:
                headers["system"].append(sys_include.group('file'))
            elif local_include:
                headers["local"].append(dir.join(local_include.group('file')))

    return headers

''' Strip local include statements and #pragma once declarations from header files '''
def file_strip(file: Path) -> str:
    new_file = ''
    strip_these = [ '#include "(?P<file>.*)"', '#pragma once' ]

    with open(str(file), mode='r') as infile:
        for line in infile:
            add_this_line = sum(re.search(strip, line) is not None for strip in strip_these) == 0

            # Change "#define CSV_INLINE" to "#define CSV_INLINE inline"
            if ('#define CSV_INLINE' in line):
                line = "#define CSV_INLINE inline\n"

            if (add_this_line):
                new_file += line

    return new_file
       
if __name__ == "__main__":
    ''' Iterate over every .cpp and .hpp file '''
    headers = []
    sources = []
    system_includes = set()

    for dir in os.walk('include'):
        files = dir[2]

        for file in files:
            fname = Path(dir[0], file)

            if (file[-4:] == '.hpp' or file[-2:] == '.h'):
                headers.append(fname)
            elif (file[-4:] == '.cpp'):
                sources.append(fname)

    # Rearrange header order to avoid compilation conflicts
    headers = header_list(headers)

    # Reorder files such that master headers are first
    MASTER_HPP = [
        Path("include", "csv.hpp"), 
        Path("include", "external", "string_view.hpp")
    ]

    for hpp in MASTER_HPP:
        headers.remove(hpp)

    headers = MASTER_HPP + headers

    # Get system includes
    for file in sources + headers:       
        for include in get_dependencies(file)['system']:
            system_includes.add(include)

    # Collate header and source files
    source_collate = ''
    header_collate = ''

    for cpp in sources:
        source_collate += file_strip(cpp) + '\n'
    
    for hpp in headers:
        header_collate += file_strip(hpp) + '\n'

    # Generate hpp file
    print("#pragma once")
    print(header_collate)
    print(source_collate)