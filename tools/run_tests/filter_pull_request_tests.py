#!/usr/bin/env python2.7
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

"""Filter out tests based on file differences compared to merge target branch"""

import re
from subprocess import call, check_output


class TestSuite:
  """
  Contains tag to identify job as belonging to this test suite and
  triggers to identify if changed files are relevant
  """
  def __init__(self, tags):
    """
    Build TestSuite to group tests by their tags
    :param tag: string used to identify if a job belongs to this TestSuite
    todo(mattkwong): Change the use of tag because do not want to depend on
    job.shortname to identify what suite a test belongs to
    """
    self.triggers = []
    self.tags = tags

  def add_trigger(self, trigger):
    """
    Add a regex to list of triggers that determine if a changed file should run tests
    :param trigger: regex matching file relevant to tests
    """
    self.triggers.append(trigger)

# Create test suites
_CORE_TEST_SUITE = TestSuite(['_c_'])
_CPP_TEST_SUITE = TestSuite(['_c++_'])
_CSHARP_TEST_SUITE = TestSuite(['_csharp_'])
_NODE_TEST_SUITE = TestSuite(['_node_'])
_OBJC_TEST_SUITE = TestSuite(['_objc_'])
_PHP_TEST_SUITE = TestSuite(['_php_', '_php7_'])
_PYTHON_TEST_SUITE = TestSuite(['_python_'])
_RUBY_TEST_SUITE = TestSuite(['_ruby'])
_ALL_TEST_SUITES = [_CORE_TEST_SUITE, _CPP_TEST_SUITE, _CSHARP_TEST_SUITE,
                    _NODE_TEST_SUITE, _OBJC_TEST_SUITE, _PHP_TEST_SUITE,
                    _PYTHON_TEST_SUITE, _RUBY_TEST_SUITE]

# Dictionary of whitelistable files where the key is a regex matching changed files
# and the value is a list of tests that should be run. An empty list means that
# the changed files should not trigger any tests. Any changed file that does not
# match any of these regexes will trigger all tests
_WHITELIST_DICT = {
  #'^templates/.*': [_sanity_test_suite],
  # todo(mattkwong): add sanity test suite
  '^doc/': [],
  '^examples/': [],
  '^summerofcode/': [],
  'README\.md$': [],
  'CONTRIBUTING\.md$': [],
  'LICENSE$': [],
  'INSTALL\.md$': [],
  'MANIFEST\.md$': [],
  'PATENTS$': [],
  'binding\.grp$': [_NODE_TEST_SUITE],
  'gRPC\-Core\.podspec$': [_OBJC_TEST_SUITE],
  'gRPC\-ProtoRPC\.podspec$': [_OBJC_TEST_SUITE],
  'gRPC\-RxLibrary\.podspec$': [_OBJC_TEST_SUITE],
  'gRPC\.podspec$': [_OBJC_TEST_SUITE],
  'composer\.json$': [_PHP_TEST_SUITE],
  'config\.m4$': [_PHP_TEST_SUITE],
  'package\.json$': [_PHP_TEST_SUITE],
  'package\.xml$': [_PHP_TEST_SUITE],
  'PYTHON\-MANIFEST\.in$': [_PYTHON_TEST_SUITE],
  'requirements\.txt$': [_PYTHON_TEST_SUITE],
  'setup\.cfg$': [_PYTHON_TEST_SUITE],
  'setup\.py$': [_PYTHON_TEST_SUITE],
  'grpc\.gemspec$': [_RUBY_TEST_SUITE],
  'Gemfile$': [_RUBY_TEST_SUITE],
  # 'grpc.def$': [_WINDOWS_TEST_SUITE],
  '^src/cpp/': [_CPP_TEST_SUITE],
  '^src/csharp/': [_CSHARP_TEST_SUITE],
  '^src/node/': [_NODE_TEST_SUITE],
  '^src/objective\-c/': [_OBJC_TEST_SUITE],
  '^src/php/': [_PHP_TEST_SUITE],
  '^src/python/': [_PYTHON_TEST_SUITE],
  '^src/ruby/': [_RUBY_TEST_SUITE],
  '^test/core/': [_CORE_TEST_SUITE],
  '^test/cpp/': [_CPP_TEST_SUITE],
  '^test/distrib/cpp/': [_CPP_TEST_SUITE],
  '^test/distrib/csharp/': [_CSHARP_TEST_SUITE],
  '^test/distrib/node/': [_NODE_TEST_SUITE],
  '^test/distrib/php/': [_PHP_TEST_SUITE],
  '^test/distrib/python/': [_PYTHON_TEST_SUITE],
  '^test/distrib/ruby/': [_RUBY_TEST_SUITE],
  '^include/grpc\+\+/': [_CPP_TEST_SUITE]
  #'^vsprojects/': [_WINDOWS_TEST_SUITE]
  # todo(mattkwong): add windows test suite
}
# Add all triggers to their respective test suites
for trigger, test_suites in _WHITELIST_DICT.iteritems():
  for test_suite in test_suites:
    test_suite.add_trigger(trigger)


def _get_changed_files(base_branch):
  """
  Get list of changed files between current branch and base of target merge branch
  """
  # git fetch might need to be called on Jenkins slave
  # todo(mattkwong): remove or uncomment below after seeing if Jenkins needs this
  call(['git', 'fetch'])

  # Get file changes between branch and merge-base of specified branch
  # Not combined to be Windows friendly
  base_commit = check_output(["git", "merge-base", base_branch, "HEAD"]).rstrip()
  return check_output(["git", "diff", base_commit, "--name-only"]).splitlines()


def _can_skip_tests(file_names, triggers):
  """
  Determines if tests are skippable based on if all files do not match list of regexes
  :param file_names: list of changed files generated by _get_changed_files()
  :param triggers: list of regexes matching file name that indicates tests should be run
  :return: safe to skip tests
  """
  for file_name in file_names:
    if any(re.match(trigger, file_name) for trigger in triggers):
      return False
  return True


def _remove_irrelevant_tests(tests, tag):
  """
  Filters out tests by config or language - will not remove sanitizer tests
  :param tests: list of all tests generated by run_tests_matrix.py
  :param tag: string representing language or config to filter - "_(language)_" or "_(config)"
  :return: list of relevant tests
  """
  # todo(mattkwong): find a more reliable way to filter tests - don't use shortname
  return [test for test in tests if tag not in test.shortname]


def filter_tests(tests, base_branch):
  """
  Filters out tests that are safe to ignore
  :param tests: list of all tests generated by run_tests_matrix.py
  :return: list of relevant tests
  """
  print("Finding file differences between %s repo and current branch...\n" % base_branch)
  changed_files = _get_changed_files(base_branch)
  for changed_file in changed_files:
    print(changed_file)
  print

  # Regex that combines all keys in _WHITELIST_DICT
  all_triggers = "(" + ")|(".join(_WHITELIST_DICT.keys()) + ")"
  # Check if all tests have to be run
  for changed_file in changed_files:
    if not re.match(all_triggers, changed_file):
      return(tests)
  # Filter out tests by language
  for test_suite in _ALL_TEST_SUITES:
    if _can_skip_tests(changed_files, test_suite.triggers):
      for tag in test_suite.tags:
        print("  Filtering %s tests" % tag)
        tests = _remove_irrelevant_tests(tests, tag)

  return tests
