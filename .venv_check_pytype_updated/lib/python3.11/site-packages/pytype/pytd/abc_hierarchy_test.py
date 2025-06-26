"""Tests for abc_hierarchy.py."""

from pytype.pytd import abc_hierarchy
import unittest


class TestAbcHierarchy(unittest.TestCase):
  """Test abc_hierarchy.py."""

  def test_get_superclasses(self):
    superclasses = abc_hierarchy.GetSuperClasses()
    self.assertDictEqual(superclasses, abc_hierarchy.SUPERCLASSES)
    # Verify that we made a copy.
    self.assertIsNot(superclasses, abc_hierarchy.SUPERCLASSES)

  def test_get_subclasses(self):
    subclasses = abc_hierarchy.GetSubClasses()
    # Check one entry.
    self.assertSetEqual(
        set(subclasses['Sized']), {'Set', 'Mapping', 'MappingView', 'Sequence'}
    )


if __name__ == '__main__':
  unittest.main()
