#!/usr/bin/env python

# Copyright 2016 gRPC authors.
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


import os
import sys
import unittest
import re

# hack import paths to pick up extra code
sys.path.insert(0, os.path.abspath('tools/run_tests/'))
from run_tests_matrix import _create_test_jobs, _create_portability_test_jobs
import python_utils.filter_pull_request_tests as filter_pull_request_tests

_LIST_OF_LANGUAGE_LABELS = ['c', 'c++', 'csharp', 'grpc-node', 'objc', 'php', 'php7', 'python', 'ruby']
_LIST_OF_PLATFORM_LABELS = ['linux', 'macos', 'windows']

class TestFilteringTest(unittest.TestCase):

  def generate_all_tests(self):
    all_jobs = _create_test_jobs() + _create_portability_test_jobs()
    self.assertIsNotNone(all_jobs)
    return all_jobs

  def test_filtering(self, changed_files=[], labels=_LIST_OF_LANGUAGE_LABELS):
    """
    Default args should filter no tests because changed_files is empty and
    default labels should be able to match all jobs
    :param changed_files: mock list of changed_files from pull request
    :param labels: list of job labels that should be skipped
    """
    all_jobs = self.generate_all_tests()
    # Replacing _get_changed_files function to allow specifying changed files in filter_tests function
    def _get_changed_files(foo):
      return changed_files
    filter_pull_request_tests._get_changed_files = _get_changed_files
    print()
    filtered_jobs = filter_pull_request_tests.filter_tests(all_jobs, "test")

    # Make sure sanity tests aren't being filtered out
    sanity_tests_in_all_jobs = 0
    sanity_tests_in_filtered_jobs = 0
    for job in all_jobs:
      if "sanity" in job.labels:
        sanity_tests_in_all_jobs += 1
    all_jobs = [job for job in all_jobs if "sanity" not in job.labels]
    for job in filtered_jobs:
      if "sanity" in job.labels:
        sanity_tests_in_filtered_jobs += 1
    filtered_jobs = [job for job in filtered_jobs if "sanity" not in job.labels]
    self.assertEquals(sanity_tests_in_all_jobs, sanity_tests_in_filtered_jobs)

    for label in labels:
      for job in filtered_jobs:
        self.assertNotIn(label, job.labels)

    jobs_matching_labels = 0
    for label in labels:
      for job in all_jobs:
        if (label in job.labels):
          jobs_matching_labels += 1
    self.assertEquals(len(filtered_jobs), len(all_jobs) - jobs_matching_labels)

  def test_individual_language_filters(self):
    # Changing unlisted file should trigger all languages
    self.test_filtering(['ffffoo/bar.baz'], [_LIST_OF_LANGUAGE_LABELS])
    # Changing core should trigger all tests
    self.test_filtering(['src/core/foo.bar'], [_LIST_OF_LANGUAGE_LABELS])
    # Testing individual languages
    self.test_filtering(['test/core/foo.bar'], [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                                                filter_pull_request_tests._CORE_TEST_SUITE.labels +
                                                filter_pull_request_tests._CPP_TEST_SUITE.labels])
    self.test_filtering(['src/cpp/foo.bar'], [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                                              filter_pull_request_tests._CPP_TEST_SUITE.labels])
    self.test_filtering(['src/csharp/foo.bar'], [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                                                 filter_pull_request_tests._CSHARP_TEST_SUITE.labels])
    self.test_filtering(['src/objective-c/foo.bar'], [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                                                      filter_pull_request_tests._OBJC_TEST_SUITE.labels])
    self.test_filtering(['src/php/foo.bar'], [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                                              filter_pull_request_tests._PHP_TEST_SUITE.labels])
    self.test_filtering(['src/python/foo.bar'], [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                                                 filter_pull_request_tests._PYTHON_TEST_SUITE.labels])
    self.test_filtering(['src/ruby/foo.bar'], [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                                               filter_pull_request_tests._RUBY_TEST_SUITE.labels])

  def test_combined_language_filters(self):
    self.test_filtering(['src/cpp/foo.bar', 'test/core/foo.bar'],
                        [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                         filter_pull_request_tests._CPP_TEST_SUITE.labels and label not in
                         filter_pull_request_tests._CORE_TEST_SUITE.labels])
    self.test_filtering(['src/cpp/foo.bar', "src/csharp/foo.bar"],
                        [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                         filter_pull_request_tests._CPP_TEST_SUITE.labels and label not in
                         filter_pull_request_tests._CSHARP_TEST_SUITE.labels])
    self.test_filtering(['src/objective-c/foo.bar', 'src/php/foo.bar', "src/python/foo.bar", "src/ruby/foo.bar"],
                        [label for label in _LIST_OF_LANGUAGE_LABELS if label not in
                         filter_pull_request_tests._OBJC_TEST_SUITE.labels and label not in
                         filter_pull_request_tests._PHP_TEST_SUITE.labels and label not in
                         filter_pull_request_tests._PYTHON_TEST_SUITE.labels and label not in
                         filter_pull_request_tests._RUBY_TEST_SUITE.labels])

  def test_platform_filter(self):
    self.test_filtering(['vsprojects/foo.bar'], [label for label in _LIST_OF_PLATFORM_LABELS if label not in
                                                 filter_pull_request_tests._WINDOWS_TEST_SUITE.labels])

  def test_whitelist(self):
    whitelist = filter_pull_request_tests._WHITELIST_DICT
    files_that_should_trigger_all_tests = ['src/core/foo.bar',
                                           'some_file_not_on_the_white_list',
                                           'BUILD',
                                           'etc/roots.pem',
                                           'Makefile',
                                           'tools/foo']
    for key in whitelist.keys():
      for file_name in files_that_should_trigger_all_tests:
        self.assertFalse(re.match(key, file_name))

if __name__ == '__main__':
  unittest.main(verbosity=2)
