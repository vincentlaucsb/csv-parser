from functools import partial
import os
import re

PARENT_DIR = os.path.split(os.getcwd())[0]
SRC_DIR = os.path.join(PARENT_DIR, 'src')

HEADERS = [ 'csv_parser.h' ]

SOURCES = [
    'csv_edit.cpp',
    'csv_reader.cpp',
    'csv_stat.cpp',
    'csv_writer.cpp',
    'data_type.cpp',
    'csv_search.cpp',
]

class SingleHeader(object):
    ''' Class for generating a single header library '''

    def __init__(self, header_files, source_files):
        self.header_files = header_files
        self.source_files = source_files
        self.includes = set([])
        self.usings = set([])
        
    def get_contents(self, filename, callback=None):
        with open(os.path.join(SRC_DIR, filename), mode='r') as infile:
            if callback:
                return ''.join([callback(line) for line in infile])
            else:
                return ''.join(infile.readlines())
                
    def include_processor(self, line):
        ''' Strip out #includes and save them for later '''

        include = r'#\s*include\s["<](.*)[">]'
        system_include = r'#\s*include\s<(.*)>'
        
        if re.search(include, line):
            # Strip out '#include "...h"'
            if re.search(system_include, line) and (line not in self.includes):
                self.includes.add(line.replace('\n', ''))
            return ''
        else:
            return line
            
    def using_processor(self, line):
        ''' Strip out 'using ...;' and save them for later '''
        using_reg = r'using (.*)::(.*);'    
        if re.search(using_reg, line):
            if line not in self.usings:
                self.usings.add(line.replace('\n', ''))
            return ''
        else:
            return line
            
    def source_processor(self, line):
        callbacks = [self.include_processor, self.using_processor]
        for f in callbacks:
            line = f(line)
        return line
                
    def generate(self):
        headers_concat = ''
        sources_concat = ''
    
        with open('scrap.hpp', mode='w') as outfile:
            for h in self.header_files:
                headers_concat += '\n' + self.get_contents(h, self.include_processor)
            
            for f in self.source_files:
                sources_concat += '\n' + self.get_contents(f, self.source_processor)
                
            # Find #includes in source files not in header                  
            for i in list(self.includes) + list(self.usings):
                outfile.write(i + '\n')
            
            outfile.write(headers_concat + '\n' + sources_concat)
            
if __name__ == '__main__':
    x = SingleHeader(HEADERS, SOURCES)
    x.generate()