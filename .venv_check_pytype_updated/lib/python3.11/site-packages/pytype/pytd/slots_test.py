"""Tests for slots.py."""

from pytype.pytd import slots
import unittest


class TestPytd(unittest.TestCase):
  """Test the operator mappings in slots.py."""

  def test_reverse_name_mapping(self):
    for operator in (
        "add",
        "and",
        "div",
        "divmod",
        "floordiv",
        "lshift",
        "matmul",
        "mod",
        "mul",
        "or",
        "pow",
        "rshift",
        "sub",
        "truediv",
        "xor",
    ):
      normal = f"__{operator}__"
      reverse = f"__r{operator}__"
      self.assertEqual(slots.REVERSE_NAME_MAPPING[normal], reverse)

  def test_symbol_mapping(self):
    for operator, symbol in [("__add__", "+"), ("__invert__", "~")]:
      self.assertEqual(slots.SYMBOL_MAPPING[operator], symbol)


if __name__ == "__main__":
  unittest.main()
