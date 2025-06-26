"""Tests for traces.source."""

from pytype.tools.traces import source
import unittest


class _FakeOpcode:

  def __init__(self, name, line):
    self.name = name
    self.line = line


class _FakeTrace(source.AbstractTrace):
  """Fake trace class for testing."""


class AbstractTraceTest(unittest.TestCase):

  def test_instantiate(self):
    with self.assertRaises(TypeError):
      source.AbstractTrace(None, None, None)
    self.assertIsInstance(_FakeTrace(None, None, None), _FakeTrace)

  def test_repr(self):
    trace = _FakeTrace("LOAD_NAME", "x", (["t"],))
    print(repr(trace))  # smoke test


class CodeTest(unittest.TestCase):
  """Tests for source.Code."""

  def test_basic(self):
    raw_traces = [(_FakeOpcode("op1", 1), "symbol1", (["data1"],)),
                  (_FakeOpcode("op2", 4), "symbol2", (["data2"],)),
                  (_FakeOpcode("op3", 1), "symbol3", (["data3"],))]
    src = source.Code("text", raw_traces, _FakeTrace, "name")
    self.assertEqual(src.text, "text")
    self.assertEqual(src.filename, "name")
    self.assertCountEqual(src.traces, [1, 4])
    self.assertEqual(
        src.traces[1],
        [_FakeTrace("op1", "symbol1", (["data1"],)),
         _FakeTrace("op3", "symbol3", (["data3"],))])
    self.assertEqual(
        src.traces[4], [_FakeTrace("op2", "symbol2", (["data2"],))])

  def test_get_offset(self):
    src = source.Code("line1\nline2", [], _FakeTrace, "")
    self.assertEqual(src.get_offset(source.Location(2, 3)), 9)

  def test_get_offset_multibyte(self):
    # With single-byte characters
    src = source.Code("""
      # coding=utf-8
      line1 # a
      line2
    """, [], _FakeTrace, "")
    self.assertEqual(src.get_offset(source.Location(4, 3)), 41)

    # With a multibyte character the byte offset should change
    src = source.Code("""
      # coding=utf-8
      line1 # ツ
      line2
    """, [], _FakeTrace, "")
    self.assertEqual(src.get_offset(source.Location(4, 3)), 43)

  def test_line(self):
    src = source.Code("line1\nline2", [], _FakeTrace, "")
    self.assertEqual(src.line(2), "line2")

  def test_get_closest_line_range(self):
    src = source.Code("line1\nline2\nline3", [], _FakeTrace, "")
    self.assertCountEqual(src.get_closest_line_range(1, 3), [1, 2])
    self.assertCountEqual(src.get_closest_line_range(2, 5), [2, 3])

  def test_find_first_text(self):
    src = source.Code("line1\nline2\nline3", [], _FakeTrace, "")
    self.assertEqual(src.find_first_text(2, 5, "line"), source.Location(2, 0))
    self.assertIsNone(src.find_first_text(2, 5, "duck"))

  def test_next_non_comment_line(self):
    src = source.Code("line1\n# line2\nline3", [], _FakeTrace, "")
    self.assertEqual(src.next_non_comment_line(1), 3)
    self.assertIsNone(src.next_non_comment_line(3))

  def test_display_traces(self):
    raw_traces = [(_FakeOpcode("op1", 1), "symbol1", (["data1"],)),
                  (_FakeOpcode("op2", 3), "symbol2", (None,))]
    src = source.Code("line1\nline2\nline3", raw_traces, _FakeTrace, "")
    src.display_traces()  # smoke test


class GetAttrLocationTest(unittest.TestCase):

  def test_one_line(self):
    src = source.Code("foo.bar", [], _FakeTrace, "")
    self.assertEqual(src.get_attr_location("foo.bar", source.Location(1, 0)),
                     (source.Location(1, 4), 3))

  def test_value_dot(self):
    src = source.Code("foo.\nbar", [], _FakeTrace, "")
    self.assertEqual(src.get_attr_location("foo.bar", source.Location(1, 0)),
                     (source.Location(2, 0), 3))

  def test_dot_attr(self):
    src = source.Code("foo\n.bar", [], _FakeTrace, "")
    self.assertEqual(src.get_attr_location("foo.bar", source.Location(1, 0)),
                     (source.Location(2, 1), 3))

  def test_not_found(self):
    src = source.Code("foo.bar", [], _FakeTrace, "")
    self.assertEqual(src.get_attr_location("foo.baz", source.Location(1, 0)),
                     (source.Location(1, 0), 7))


if __name__ == "__main__":
  unittest.main()
