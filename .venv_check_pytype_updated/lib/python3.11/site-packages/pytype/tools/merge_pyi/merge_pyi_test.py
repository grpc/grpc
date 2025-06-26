import collections
import difflib
import logging
import os
import re

from pytype.platform_utils import path_utils
from pytype.tools.merge_pyi import merge_pyi
import unittest


__all__ = ('TestBuilder', 'load_tests')


PY, PYI, EXPECTED = 'py', 'pyi', 'pep484.py'
OVERWRITE_EXPECTED = 0  # flip to regenerate expected files


def load_tests(unused_loader, standard_tests, unused_pattern):
  root = path_utils.join(path_utils.dirname(__file__), 'test_data')
  standard_tests.addTests(TestBuilder().build(root))
  return standard_tests


class TestBuilder:

  def build(self, data_dir):
    """Return a unittest.TestSuite with tests for the files in data_dir."""

    suite = unittest.TestSuite()

    files_by_base = self._get_files_by_base(data_dir)

    for base, files_by_ext in sorted(files_by_base.items()):
      if not (PY in files_by_ext and PYI in files_by_ext):
        continue

      if not OVERWRITE_EXPECTED and EXPECTED not in files_by_ext:
        continue

      py, pyi = (files_by_ext[x] for x in (PY, PYI))
      outfile = path_utils.join(data_dir, base + '.' + EXPECTED)

      test = build_regression_test(py, pyi, outfile)
      suite.addTest(test)

    return suite

  def _get_files_by_base(self, data_dir):
    files = os.listdir(data_dir)

    file_pat = re.compile(r'(?P<filename>(?P<base>.+?)\.(?P<ext>.*))$')
    matches = [m for m in map(file_pat.match, files) if m]
    ret = collections.defaultdict(dict)
    for m in matches:
      base, ext, filename = m.group('base'), m.group('ext'), m.group('filename')
      ret[base][ext] = path_utils.join(data_dir, filename)

    return ret


def build_regression_test(py, pyi, outfile):

  def regression_test(test_case):
    py_input, pyi_src = (_read_file(f) for f in (py, pyi))
    try:
      output = merge_pyi.merge_sources(py=py_input, pyi=pyi_src)
    except merge_pyi.MergeError:
      pass

    if OVERWRITE_EXPECTED:
      with open(outfile, 'w') as f:
        f.write(output)
    else:
      expected = _read_file(outfile)
      test_case.assertEqual(expected, output, _get_diff(expected, output))

  name = path_utils.splitext(path_utils.basename(outfile))[0].replace('.', '_')
  test = f'test_{name}'
  case = type('RegressionTest', (unittest.TestCase,), {test: regression_test})
  return case(test)


def _read_file(filename):
  with open(filename) as f:
    return f.read()


def _get_diff(a, b):
  a, b = a.split('\n'), b.split('\n')

  diff = difflib.Differ().compare(a, b)
  return '\n'.join(diff)


if __name__ == '__main__':
  logging.basicConfig(level=logging.CRITICAL)
  unittest.main()
