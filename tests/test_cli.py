'''
Tests of Command Line Interface
'''

import pexpect
import unittest
import os

PARENT_DIR = os.path.split(os.getcwd())[0]
os.chdir(PARENT_DIR)
    
class CSVPrintTest(unittest.TestCase):
    ''' Test printing out a CSV '''
        
    def test_first_row(self):
        child = pexpect.spawn(
            './csv_parser tests/data/fake_data/ints.csv')
        self.assertFalse(child.expect(
            '\[0\]   A   B   C   D   E   F   G   H   I   J   '
        ))
        
    def test_grep(self):
        # Regex search for two digit numbers
        child = pexpect.spawn(
            './csv_parser grep tests/data/fake_data/ints.csv A "\d\d"'
        )
        
        self.assertFalse(child.expect(
            '20\s+20\s+20'
        ))
        
'''
if __name__ == '__main__':
    unittest.main()
'''