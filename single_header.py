import os
import re

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
                self.files.append(os.path.join(dir[0], file))

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
    # Set of "#include <...>"
    sys_includes = set()

    # Mapping of strings to namespace contents
    namespaces = dict()

    parser = Sources()

    for line in parser:
        # Deal with includes
        rmatch = re.match('#include <.*>', line)
        if (rmatch):
            # Extract include text w/o extraneous whitespace
            sys_includes.add(rmatch.group(0))

        rmatch = re.match('namespace', line)
        if (rmatch):
            rmatch = re.search('namespace (?P<name>(.*)) {(?P<code>(\n.*)*)}', parser.current_source)
            if (rmatch):
                name = rmatch.group('name')
                if name in namespaces:
                    namespaces[name] += rmatch.group('code')
                else:
                    namespaces[name] = rmatch.group('code')

            parser.skip_to_next_file()

    # Create single header file
    sys_includes = list(sys_includes)
    sys_includes.sort()

    for i in sys_includes:
        print(i)

    for k in namespaces:
        formatted = "namespace {name} {{ {code} }}"
        print(formatted.format(
            name=k,
            code=namespaces[k]
        ))