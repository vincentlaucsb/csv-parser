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

            if 'namespace' in line:
                break

    return headers

class Sources(object):
    def __init__(self):
        self.files = []
        self.current_lines = []
        self.current_file = None
        self.current_source = None
        
        ''' Iterate over every .cpp and .hpp file '''
        for dir in os.walk('src'):
            files = dir[2]

            for file in files:
                self.files.append(Path(dir[0], file))

    def __iter__(self):
        return self

    def skip_to_next_file(self):
        self.current_lines.clear()

    def __next__(self):
        if (not self.current_lines):
            if (self.files):
                self.current_file = self.files.pop()
                with open(self.current_file) as infile:
                    self.current_lines = infile.readlines()
                    self.current_source = ''.join(self.current_lines)
            else:
                raise StopIteration

        return self.current_lines.pop()
       
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

    # Get system includes
    for file in sources + headers:       
        for include in get_dependencies(file)['system']:
            system_includes.add(include)

    # Mapping of strings to namespace contents
    source_namespaces = dict()

    for cpp in sources:
        with open(str(cpp), mode='r') as infile:
            rmatch = re.search('namespace (?P<name>(.*)) {(?P<code>(\n.*)*)}', infile.read())
            if (rmatch):
                name = rmatch.group('name')
                if name in source_namespaces:
                    source_namespaces[name] += rmatch.group('code')
                else:
                    source_namespaces[name] = rmatch.group('code')

    # Mapping of strings to namespace contents
    header_namespaces = dict()
    
    for hpp in headers:
        with open(str(hpp), mode='r') as infile:
            rmatch = re.search('namespace (?P<name>(.*)) {(?P<code>(\n.*)*)}', infile.read())
            if (rmatch):
                name = rmatch.group('name')
                if name in header_namespaces:
                    header_namespaces[name] += rmatch.group('code')
                else:
                    header_namespaces[name] = rmatch.group('code')

    # Generate hpp file
    system_includes = list(system_includes)
    system_includes.sort()
    for i in system_includes:
        print("#include <{}>".format(i))

    # Collate header source code
    for k in header_namespaces:
        formatted = "namespace {name} {{ {code} }}"
        print(formatted.format(
            name=k,
            code=header_namespaces[k]
        ))

    # Collate source code
    for k in source_namespaces:
        formatted = "namespace {name} {{ {code} }}"
        print(formatted.format(
            name=k,
            code=source_namespaces[k]
        ))