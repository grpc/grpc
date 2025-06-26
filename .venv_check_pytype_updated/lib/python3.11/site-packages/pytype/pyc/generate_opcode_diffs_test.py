"""Test pyc/generate_opcode_diffs.py."""

import json
import subprocess
import textwrap
import types
from unittest import mock

from pytype.pyc import generate_opcode_diffs

import unittest


class GenerateOpcodeDiffsTest(unittest.TestCase):

  def _generate_diffs(self):
    with mock.patch.object(subprocess, 'run') as mock_run:
      more_opcode_names = [
          f'<{i}>'
          for i in range(9, generate_opcode_diffs._MIN_INSTRUMENTED_OPCODE)
      ]
      mapping_38 = json.dumps({
          'opmap': {'DO_THIS': 1, 'I_MOVE': 2, 'DO_EIGHT': 5, 'JUMP': 8},
          'opname': [
              '<0>',
              'DO_THIS',
              'I_MOVE',
              '<3>',
              '<4>',
              'DO_EIGHT',
              '<6>',
              '<7>',
              'JUMP',
          ] + more_opcode_names,
          'intrinsic_1_descs': ['INTRINSIC_1_INVALID', 'INTRINSIC_PRINT'],
          'intrinsic_2_descs': ['INTRINSIC_2_INVALID'],
          'inline_cache_entries': [0, 0, 1, 0, 0, 8, 0, 0, 2],
          'HAVE_ARGUMENT': 3,
          'HAS_CONST': [],
          'HAS_NAME': [],
          'HAS_JREL': [],
      })
      mapping_39 = json.dumps({
          'opmap': {
              'I_MOVE': 3,
              'DO_THAT': 4,
              'DO_THAT_TOO': 5,
              'DO_NINE': 7,
              'JUMP': 8,
          },
          'opname': [
              '<0>',
              '<1>',
              '<2>',
              'I_MOVE',
              'DO_THAT',
              'DO_THAT_TOO',
              '<6>',
              'DO_NINE',
              'JUMP',
          ] + more_opcode_names,
          'intrinsic_1_descs': ['INTRINSIC_1_INVALID', 'INTRINSIC_IMPORT_STAR'],
          'intrinsic_2_descs': ['INTRINSIC_2_INVALID'],
          'inline_cache_entries': [0, 0, 0, 1, 0, 0, 0, 9, 3],
          'HAVE_ARGUMENT': 6,
          'HAS_CONST': [7],
          'HAS_NAME': [5, 7],
          'HAS_JREL': [8],
      })
      mock_run.side_effect = [
          types.SimpleNamespace(stdout=mapping_38),
          types.SimpleNamespace(stdout=mapping_39),
      ]
      return generate_opcode_diffs.generate_diffs(['3.8', '3.9'])

  def test_classes(self):
    classes, _, _, _, _, _, _, _ = self._generate_diffs()
    i_move, do_that, do_that_too, do_nine, jump = classes
    self.assertMultiLineEqual(
        '\n'.join(i_move),
        textwrap.dedent("""
      class I_MOVE(Opcode):
        __slots__ = ()
    """).strip(),
    )
    self.assertMultiLineEqual(
        '\n'.join(do_that),
        textwrap.dedent("""
      class DO_THAT(Opcode):
        __slots__ = ()
    """).strip(),
    )
    self.assertMultiLineEqual(
        '\n'.join(do_that_too),
        textwrap.dedent("""
          class DO_THAT_TOO(Opcode):
            _FLAGS = HAS_NAME
            __slots__ = ()
        """).strip(),
    )
    self.assertMultiLineEqual(
        '\n'.join(do_nine),
        textwrap.dedent("""
          class DO_NINE(OpcodeWithArg):
            _FLAGS = HAS_ARGUMENT | HAS_CONST | HAS_NAME
            __slots__ = ()
        """).strip(),
    )
    self.assertMultiLineEqual(
        '\n'.join(jump),
        textwrap.dedent("""
          class JUMP(OpcodeWithArg):
            _FLAGS = HAS_ARGUMENT | HAS_JREL
            __slots__ = ()
        """).strip(),
    )

  def test_stubs(self):
    _, stubs, _, _, _, _, _, _ = self._generate_diffs()
    do_that, do_that_too, do_nine, intrinsic_import_star = stubs
    self.assertMultiLineEqual(
        '\n'.join(do_that),
        textwrap.dedent("""
      def byte_DO_THAT(self, state, op):
        del op
        return state
    """).strip(),
    )
    self.assertMultiLineEqual(
        '\n'.join(do_that_too),
        textwrap.dedent("""
      def byte_DO_THAT_TOO(self, state, op):
        del op
        return state
    """).strip(),
    )
    self.assertMultiLineEqual(
        '\n'.join(do_nine),
        textwrap.dedent("""
      def byte_DO_NINE(self, state, op):
        del op
        return state
    """).strip(),
    )
    self.assertMultiLineEqual(
        '\n'.join(intrinsic_import_star),
        textwrap.dedent("""
      def byte_INTRINSIC_IMPORT_STAR(self, state):
        return state
    """).strip(),
    )

  def test_impl_changed(self):
    _, _, impl_changed, _, _, _, _, _ = self._generate_diffs()
    self.assertEqual(impl_changed, ['I_MOVE', 'JUMP'])

  def test_mapping(self):
    _, _, _, mapping, _, _, _, _ = self._generate_diffs()
    self.assertMultiLineEqual(
        '\n'.join(mapping),
        textwrap.dedent("""
          1: None,  # was DO_THIS in 3.8
          2: None,  # was I_MOVE in 3.8
          3: "I_MOVE",
          4: "DO_THAT",
          5: "DO_THAT_TOO",  # was DO_EIGHT in 3.8
          7: "DO_NINE",
        """).strip(),
    )

  def test_arg_types(self):
    _, _, _, _, arg_types, _, _, _ = self._generate_diffs()
    self.assertMultiLineEqual(
        '\n'.join(arg_types),
        textwrap.dedent("""
          "DO_THAT_TOO": NAME,
          "DO_NINE": CONST,
          "JUMP": JREL,
        """).strip(),
    )

  def test_intrinsic_descs(self):
    _, _, _, _, _, descs_1, descs_2, _ = self._generate_diffs()
    self.assertMultiLineEqual(
        '\n'.join(descs_1 + descs_2),
        textwrap.dedent("""
          PYTHON_3_9_INTRINSIC_1_DESCS = [
              "INTRINSIC_1_INVALID",
              "INTRINSIC_IMPORT_STAR",
          ]
        """).strip(),
    )

  def test_inline_cache_entries(self):
    _, _, _, _, _, _, _, inline_cache_entries = self._generate_diffs()
    self.assertMultiLineEqual(
        '\n'.join(inline_cache_entries),
        textwrap.dedent("""
          "DO_NINE": 9,
          "JUMP": 3,
        """).strip(),
    )


if __name__ == '__main__':
  unittest.main()
