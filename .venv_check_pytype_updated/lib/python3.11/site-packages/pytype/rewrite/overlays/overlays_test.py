from pytype.rewrite.abstract import abstract
from pytype.rewrite.overlays import overlays

import unittest


class OverlaysTest(unittest.TestCase):

  def test_register_function(self):

    @overlays.register_function('test_mod', 'test_func')
    class TestFunc(abstract.PytdFunction):
      pass

    expected_key = ('test_mod', 'test_func')
    self.assertIn(expected_key, overlays.FUNCTIONS)
    self.assertEqual(overlays.FUNCTIONS[expected_key], TestFunc)

  def test_register_class_transform(self):

    @overlays.register_class_transform(inheritance_hook='test.Class')
    def transform_cls(ctx, cls):
      del ctx, cls  # unused

    self.assertIn('test.Class', overlays.CLASS_TRANSFORMS)
    self.assertEqual(overlays.CLASS_TRANSFORMS['test.Class'], transform_cls)


if __name__ == '__main__':
  unittest.main()
