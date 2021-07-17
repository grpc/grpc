#!/usr/bin/env python
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
"""Filter out tests based on file differences compared to merge target branch"""

from __future__ import print_function

import re
import six
import subprocess


class TestSuite:
    """
  Contains label to identify job as belonging to this test suite and
  triggers to identify if changed files are relevant
  """

    def __init__(self, labels):
        """
    Build TestSuite to group tests based on labeling
    :param label: strings that should match a jobs's platform, config, language, or test group
    """
        self.triggers = []
        self.labels = labels

    def add_trigger(self, trigger):
        """
    Add a regex to list of triggers that determine if a changed file should run tests
    :param trigger: regex matching file relevant to tests
    """
        self.triggers.append(trigger)


# Create test suites
_CORE_TEST_SUITE = TestSuite(['c'])
_CPP_TEST_SUITE = TestSuite(['c++'])
_CSHARP_TEST_SUITE = TestSuite(['csharp'])
_NODE_TEST_SUITE = TestSuite(['grpc-node'])
_OBJC_TEST_SUITE = TestSuite(['objc'])
_PHP_TEST_SUITE = TestSuite(['php', 'php7'])
_PYTHON_TEST_SUITE = TestSuite(['python'])
_RUBY_TEST_SUITE = TestSuite(['ruby'])
_LINUX_TEST_SUITE = TestSuite(['linux'])
_WINDOWS_TEST_SUITE = TestSuite(['windows'])
_MACOS_TEST_SUITE = TestSuite(['macos'])
_ALL_TEST_SUITES = [
    _CORE_TEST_SUITE, _CPP_TEST_SUITE, _CSHARP_TEST_SUITE, _NODE_TEST_SUITE,
    _OBJC_TEST_SUITE, _PHP_TEST_SUITE, _PYTHON_TEST_SUITE, _RUBY_TEST_SUITE,
    _LINUX_TEST_SUITE, _WINDOWS_TEST_SUITE, _MACOS_TEST_SUITE
]

# Dictionary of allowlistable files where the key is a regex matching changed files
# and the value is a list of tests that should be run. An empty list means that
# the changed files should not trigger any tests. Any changed file that does not
# match any of these regexes will trigger all tests
# DO NOT CHANGE THIS UNLESS YOU KNOW WHAT YOU ARE DOING (be careful even if you do)
_ALLOWLIST_DICT = {
    '^doc/': [],
    '^examples/': [],
    '^include/grpc\+\+/': [_CPP_TEST_SUITE],
    '^include/grpcpp/': [_CPP_TEST_SUITE],
    '^summerofcode/': [],
    '^src/cpp/': [_CPP_TEST_SUITE],
    '^src/csharp/': [_CSHARP_TEST_SUITE],
    '^src/objective\-c/': [_OBJC_TEST_SUITE],
    '^src/php/': [_PHP_TEST_SUITE],
    '^src/python/': [_PYTHON_TEST_SUITE],
    '^src/ruby/': [_RUBY_TEST_SUITE],
    '^templates/': [],
    '^test/core/': [_CORE_TEST_SUITE, _CPP_TEST_SUITE],
    '^test/cpp/': [_CPP_TEST_SUITE],
    '^test/distrib/cpp/': [_CPP_TEST_SUITE],
    '^test/distrib/csharp/': [_CSHARP_TEST_SUITE],
    '^test/distrib/php/': [_PHP_TEST_SUITE],
    '^test/distrib/python/': [_PYTHON_TEST_SUITE],
    '^test/distrib/ruby/': [_RUBY_TEST_SUITE],
    '^tools/run_tests/xds_k8s_test_driver/': [],
    '^tools/internal_ci/linux/grpc_xds_k8s.*': [],
    '^vsprojects/': [_WINDOWS_TEST_SUITE],
    'composer\.json$': [_PHP_TEST_SUITE],
    'config\.m4$': [_PHP_TEST_SUITE],
    'CONTRIBUTING\.md$': [],
    'Gemfile$': [_RUBY_TEST_SUITE],
    'grpc\.def$': [_WINDOWS_TEST_SUITE],
    'grpc\.gemspec$': [_RUBY_TEST_SUITE],
    'gRPC\.podspec$': [_OBJC_TEST_SUITE],
    'gRPC\-Core\.podspec$': [_OBJC_TEST_SUITE],
    'gRPC\-ProtoRPC\.podspec$': [_OBJC_TEST_SUITE],
    'gRPC\-RxLibrary\.podspec$': [_OBJC_TEST_SUITE],
    'BUILDING\.md$': [],
    'LICENSE$': [],
    'MANIFEST\.md$': [],
    'package\.json$': [_PHP_TEST_SUITE],
    'package\.xml$': [_PHP_TEST_SUITE],
    'PATENTS$': [],
    'PYTHON\-MANIFEST\.in$': [_PYTHON_TEST_SUITE],
    'README\.md$': [],
    'requirements\.txt$': [_PYTHON_TEST_SUITE],
    'setup\.cfg$': [_PYTHON_TEST_SUITE],
    'setup\.py$': [_PYTHON_TEST_SUITE]
}

# Regex that combines all keys in _ALLOWLIST_DICT
_ALL_TRIGGERS = "(" + ")|(".join(_ALLOWLIST_DICT.keys()) + ")"

# Add all triggers to their respective test suites
for trigger, test_suites in six.iteritems(_ALLOWLIST_DICT):
    for test_suite in test_suites:
        test_suite.add_trigger(trigger)


def _get_changed_files(base_branch):
    """
  Get list of changed files between current branch and base of target merge branch
  """
    # Get file changes between branch and merge-base of specified branch
    # Not combined to be Windows friendly
    base_commit = subprocess.check_output(
        ["git", "merge-base", base_branch, "HEAD"]).decode("UTF-8").rstrip()
    return subprocess.check_output(
        ["git", "diff", base_commit, "--name-only",
         "HEAD"]).decode("UTF-8").splitlines()


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


def _remove_irrelevant_tests(tests, skippable_labels):
    """
  Filters out tests by config or language - will not remove sanitizer tests
  :param tests: list of all tests generated by run_tests_matrix.py
  :param skippable_labels: list of languages and platforms with skippable tests
  :return: list of relevant tests
  """
    # test.labels[0] is platform and test.labels[2] is language
    # We skip a test if both are considered safe to skip
    return [test for test in tests if test.labels[0] not in skippable_labels or \
            test.labels[2] not in skippable_labels]


def affects_c_cpp(base_branch):
    """
  Determines if a pull request's changes affect C/C++. This function exists because
  there are pull request tests that only test C/C++ code
  :param base_branch: branch that a pull request is requesting to merge into
  :return: boolean indicating whether C/C++ changes are made in pull request
  """
    changed_files = _get_changed_files(base_branch)
    # Run all tests if any changed file is not in the allowlist dictionary
    for changed_file in changed_files:
        if not re.match(_ALL_TRIGGERS, changed_file):
            return True
    return not _can_skip_tests(
        changed_files, _CPP_TEST_SUITE.triggers + _CORE_TEST_SUITE.triggers)


def filter_tests(tests, base_branch):
    """
  Filters out tests that are safe to ignore
  :param tests: list of all tests generated by run_tests_matrix.py
  :return: list of relevant tests
  """
    print(
        'Finding file differences between gRPC %s branch and pull request...\n'
        % base_branch)
    changed_files = _get_changed_files(base_branch)
    for changed_file in changed_files:
        print('  %s' % changed_file)
    print('')

    # Run all tests if any changed file is not in the allowlist dictionary
    for changed_file in changed_files:
        if not re.match(_ALL_TRIGGERS, changed_file):
            return (tests)
    # Figure out which language and platform tests to run
    skippable_labels = []
    for test_suite in _ALL_TEST_SUITES:
        if _can_skip_tests(changed_files, test_suite.triggers):
            for label in test_suite.labels:
                print('  %s tests safe to skip' % label)
                skippable_labels.append(label)
    tests = _remove_irrelevant_tests(tests, skippable_labels)
    return tests
