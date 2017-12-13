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
            '/home/csv-parser/bin/csv_parser /home/csv-parser/tests/data/fake_data/ints.csv',
            timeout=1)
            
        self.assertFalse(child.expect(
            '\[0\]\s+A\s+B\s+C\s+D\s+E\s+F\s+G\s+H\s+I\s+J\s+'
        ))
        
    def test_grep(self):
        # Regex search for two digit numbers
        child = pexpect.spawn(
            '/home/csv-parser/bin/csv_parser grep /home/csv-parser/tests/data/fake_data/ints.csv A "\d\d"'
        )
        
        child.interact()
        
        self.assertFalse(child.expect(
            '20\s+20\s+20'
        ))
        
if __name__ == '__main__':
    unittest.main()