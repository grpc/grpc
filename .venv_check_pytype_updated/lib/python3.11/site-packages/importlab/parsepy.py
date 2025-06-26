# Copyright 2017 Google Inc.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

"""Logic for resolving import paths."""

import collections
import logging
import sys

from . import import_finder
from . import utils


class ParseError(Exception):
    """Error parsing a file with python."""
    pass


class ImportStatement(collections.namedtuple(
        'ImportStatement',
        ['name', 'new_name', 'is_from', 'is_star', 'source'])):
    """A Python import statement, such as "import foo as bar"."""

    def __new__(cls, name, new_name=None, is_from=False, is_star=False,
                source=None):
        """Create a new ImportStatement.

        Args:
          name: Name of the module to be imported. E.g. "sys".
          new_name: What the module is renamed to. The "y" in
            "import x as y".
          is_from: If the last part of the name (the "z" in "x.y.z") can
          be an element within a module, instead of a module itself. Happens
          e.g. for "from sys import argv".
          is_star: If this is an import of the form "from x import *".
          source: The path to the file as resolved by python.
        Returns:
          A new ImportStatement instance.
        """
        return super(ImportStatement, cls).__new__(
            cls, name, new_name or name, is_from, is_star, source)

    def is_relative(self):
        return self.name.startswith('.')

    def __str__(self):
        if self.is_star:
            assert self.name == self.new_name
            assert self.is_from
            return 'from ' + self.name + ' import *'
        if self.is_from:
            try:
                left, right = self.name.rsplit('.', 2)
            except ValueError:
                left, right = self.name, ''
            module = left + '[.' + right + ']'
        else:
            module = self.name
        if self.new_name != self.name:
            return 'import ' + module + ' as ' + self.new_name
        else:
            return 'import ' + module


def get_imports(filename, python_version):
    if python_version == sys.version_info[0:2]:
        # Invoke import_finder directly
        try:
            imports = import_finder.get_imports(filename)
        except Exception:
            raise ParseError(filename)
    else:
        # Call the appropriate python version in a subprocess
        f = sys.modules['importlab.import_finder'].__file__
        if f.rsplit('.', 1)[-1] == 'pyc':
            # In host Python 2, importlab ships with .pyc files.
            f = f[:-1]
        ret, stdout, stderr = utils.run_py_file(python_version, f, filename)
        if not ret:
            if sys.version_info[0] == 3:
                stdout = stdout.decode('ascii')
            imports = import_finder.read_imports(stdout)
        else:
            if sys.version_info[0] == 3:
                stderr = stderr.decode('ascii')
            logging.info(stderr)
            raise ParseError(filename)
    return [ImportStatement(*imp) for imp in imports]
