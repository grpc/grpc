import unittest

class TestBad(unittest.TestCase):

    def test_upper(self):
        self.assertEqual('foo'.upper(), 'BAD')

if __name__ == '__main__':
    unittest.main()
