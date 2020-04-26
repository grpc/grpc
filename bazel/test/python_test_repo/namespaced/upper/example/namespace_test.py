import logging
import unittest

import NamespacedExample


class ImportTest(unittest.TestCase):
  def test_import(self):
    namespaced_example = NamespacedExample()
    namespaced_example.value = "hello"
    # Dummy assert, important part is namespaced example was imported.
    self.assertEqual(namespaced_example.value, "hello")


if __name__ == '__main__':
  logging.basicConfig()
  unittest.main()
