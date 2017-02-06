# Copyright 2015, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

from __future__ import absolute_import

import importlib
import pkgutil
import re
import unittest

import coverage

TEST_MODULE_REGEX = r'^.*_test$'


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
        modules = [importlib.import_module(name) for name in names]
        for module in modules:
            self.visit_module(module)
        for module in modules:
            try:
                package_paths = module.__path__
            except:
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
        for importer, module_name, is_package in (
                pkgutil.walk_packages(package_paths)):
            module = importer.find_module(module_name).load_module(module_name)
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
                'unexpected suite item of type {}'.format(type(item)))
