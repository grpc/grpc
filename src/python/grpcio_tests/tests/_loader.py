# Copyright 2015 gRPC authors.
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

from __future__ import absolute_import

import importlib
import os
import pkgutil
import re
import sys
import unittest

import coverage

TEST_MODULE_REGEX = r"^.*_test$"


# Determines the path og a given path relative to the first matching
# path on sys.path. Useful for determining what a directory's module
# path will be.
def _relativize_to_sys_path(path):
    for sys_path in sys.path:
        if path.startswith(sys_path):
            relative = path[len(sys_path) :]
            if not relative:
                return ""
            if relative.startswith(os.path.sep):
                relative = relative[len(os.path.sep) :]
            if not relative.endswith(os.path.sep):
                relative += os.path.sep
            return relative
    raise AssertionError(f"Failed to relativize {path} to sys.path.")


def _relative_path_to_module_prefix(path):
    return path.replace(os.path.sep, ".")


class Loader(object):
    """Test loader for setuptools test suite support.

    Attributes:
      suite (unittest.TestSuite): All tests collected by the loader.
      loader (unittest.TestLoader): Standard Python unittest loader to be ran per
        module discovered.
      module_matcher (re.RegexObject): A regular expression object to match
        against module names and determine whether or not the discovered module
        contributes to the test suite.
    """

    def __init__(self):
        self.suite = unittest.TestSuite()
        self.loader = unittest.TestLoader()
        self.module_matcher = re.compile(TEST_MODULE_REGEX)

    def loadTestsFromNames(self, names, module=None):
        """Function mirroring TestLoader::loadTestsFromNames, as expected by
        setuptools.setup argument `test_loader`."""
        # ensure that we capture decorators and definitions (else our coverage
        # measure unnecessarily suffers)
        coverage_context = coverage.Coverage(data_suffix=True)
        coverage_context.start()
        imported_modules = tuple(
            importlib.import_module(name) for name in names
        )
        for imported_module in imported_modules:
            self.visit_module(imported_module)
        for imported_module in imported_modules:
            try:
                package_paths = imported_module.__path__
            except AttributeError:
                continue
            self.walk_packages(package_paths)
        coverage_context.stop()
        coverage_context.save()
        return self.suite

    def walk_packages(self, package_paths):
        """Walks over the packages, dispatching `visit_module` calls.

        Args:
          package_paths (list): A list of paths over which to walk through modules
            along.
        """
        for path in package_paths:
            self._walk_package(path)

    def _walk_package(self, package_path):
        prefix = _relative_path_to_module_prefix(
            _relativize_to_sys_path(package_path)
        )
        for importer, module_name, is_package in pkgutil.walk_packages(
            [package_path], prefix
        ):
            found_module = importer.find_module(module_name)
            module = None
            if module_name in sys.modules:
                module = sys.modules[module_name]
            else:
                module = found_module.load_module(module_name)
            self.visit_module(module)

    def visit_module(self, module):
        """Visits the module, adding discovered tests to the test suite.

        Args:
          module (module): Module to match against self.module_matcher; if matched
            it has its tests loaded via self.loader into self.suite.
        """
        if self.module_matcher.match(module.__name__):
            module_suite = self.loader.loadTestsFromModule(module)
            self.suite.addTest(module_suite)


def iterate_suite_cases(suite):
    """Generator over all unittest.TestCases in a unittest.TestSuite.

    Args:
      suite (unittest.TestSuite): Suite to iterate over in the generator.

    Returns:
      generator: A generator over all unittest.TestCases in `suite`.
    """
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            for child_item in iterate_suite_cases(item):
                yield child_item
        elif isinstance(item, unittest.TestCase):
            yield item
        else:
            raise ValueError(
                f"unexpected suite item of type {type(item)}"
            )
