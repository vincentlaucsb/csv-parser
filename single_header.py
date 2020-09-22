from collections import namedtuple
import os
import re

CPP_SEP = '/'
Include = namedtuple('Include', ['path', 'line_no'])

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
            dependencies[file] = [d.path for d in get_dependencies(file)['local']]

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
        for i, line in enumerate(infile):
            sys_include = re.search('^#include <(?P<file>.*)>', line)
            local_include = re.search('^#include "(?P<file>.*)"', line)
            if sys_include:
                headers["system"].append(
                    Include(path=sys_include.group('file'), line_no=i))
            elif local_include:
                headers["local"].append(
                    Include(path=dir.join(local_include.group('file')), line_no=i))

    return headers

''' Strip local include statements and #pragma once declarations from source files '''
def file_strip(file: Path) -> str:
    new_file = ''
    strip_these = ['#include "(?P<file>.*)"', '#pragma once' ]

    # Strip out pragma once
    with open(str(file), mode='r') as infile:
        for line in infile:
            add_this_line = sum(re.search(strip, line) is not None for strip in strip_these) == 0

            # Change "#define CSV_INLINE" to "#define CSV_INLINE inline"
            if ('#define CSV_INLINE' in line):
                line = "#define CSV_INLINE inline\n"

            if (add_this_line):
                new_file += line

    return new_file

'''
Collate header files by using this following algorithm:

- Given a list of header files (HEADERS) ordered such that the first file
    has no internal dependencies, and the last file is the most dependent
    - Reverse the list
- Maintain these data structures:
    - A set of header files (PROCESSED) that were processed
    - A set of header files (MISSING_INCLUDES) that we are looking for
    - The collation of header source code (HEADER_CONCAT)
- Go through each FILE in list of headers in reverse order (starting with
  the headers at the highest level of the dependency tree)
    - If FILE is not in MISSING_INCLUDES, then concatenate source verbatim to HEADER_CONCAT
    - Otherwise, there is one or more #include statements in HEADER_CONCAT which references FILE 
        - Replace the first #include statement with the source of FILE, and remove the rest
'''
def header_collate(headers: list):
    headers.reverse()

    # Placeholder for includes to be inserted
    splice_template = "__INSERT_HEADER_HERE__({})\n"
    header_concat = ''
    processed = set()
    missing_includes = set()

    def process_file(path: Path):
        source = ''

        with open(str(path), mode='r') as infile:
            for line in infile:
                # Add local includes to MISSING_INCLUDES
                local_include = re.search('^#include "(?P<file>.*)"', line)
                if local_include:
                    dir = Path(path[:-1])
                    include_path = dir.join(local_include.group('file'))

                    if str(include_path) not in processed:
                        missing_includes.add(str(include_path))
                        source += splice_template.format(str(include_path))
                elif '#pragma once' in line:
                    continue
                else:
                    source += line

        return source

    for path in headers:
        processed.add(str(path))

        if str(path) in missing_includes:
            source = process_file(path)
            splice_phrase = splice_template.format(str(path))
            header_concat = header_concat.replace(
                splice_phrase,
                source + '\n', 1)
            header_concat = header_concat.replace(splice_phrase, '')

            missing_includes.remove(str(path))
        else:
            header_concat += process_file(path)

    return header_concat

if __name__ == "__main__":
    ''' Iterate over every .cpp and .hpp file '''
    headers = []
    sources = []
    system_includes = set()

    # Generate a list of header and source file locations
    for dir in os.walk('include'):
        files = dir[2]

        for file in files:
            fname = Path(dir[0], file)

            if (file[-4:] == '.hpp' or file[-2:] == '.h'):
                headers.append(fname)
            elif (file[-4:] == '.cpp'):
                sources.append(fname)

    # Rearrange header order to avoid compilation conflicts
    headers = header_list(sorted(headers))

    # Get system includes
    for file in sources + headers:
        for include in get_dependencies(file)['system']:
            system_includes.add(include.path)

    # Collate header and source files
    header_concat = header_collate(headers)
    source_collate = ''

    for cpp in sources:
        source_collate += file_strip(cpp) + '\n'
    
    # Generate hpp file
    print("#pragma once")
    print(header_concat.replace(
        "#define CSV_INLINE", "#define CSV_INLINE inline").replace(
            "/** INSERT_CSV_SOURCES **/", source_collate))