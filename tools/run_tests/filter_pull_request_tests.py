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

from subprocess import call, check_output

# Whitelist for all tests
# Update all instances in corresponding trigger lists when modifying this
starts_with_whitelist = ['templates/',
                         'doc/',
                         'examples/',
                         'summerofcode/',
                         'src/cpp',
                         'src/csharp',
                         'src/node',
                         'src/objective-c',
                         'src/php',
                         'src/python',
                         'src/ruby',
                         'test/core',
                         'test/cpp',
                         'test/distrib/cpp',
                         'test/distrib/csharp',
                         'test/distrib/node',
                         'test/distrib/php',
                         'test/distrib/python',
                         'test/distrib/ruby']
				   
ends_with_whitelist = ['README.md',
                       'LICENSE']

# Triggers for core tests
core_starts_with_triggers = ['test/core']

# Triggers for c++ tests
cpp_starts_with_triggers = ['src/cpp',
                            'test/cpp',
                            'test/distrib/cpp']

# Triggers for c# tests
csharp_starts_with_triggers = ['src/csharp',
                               'test/distrib/csharp']

# Triggers for node tests
node_starts_with_triggers = ['src/node',
                             'test/distrib/node']

# Triggers for objective-c tests
objc_starts_with_triggers = ['src/objective-c']

# Triggers for php tests
php_starts_with_triggers = ['src/php',
                            'test/distrib/php']

# Triggers for python tests
python_starts_with_triggers = ['src/python',
                               'test/distrib/python']

# Triggers for ruby tests
ruby_starts_with_triggers = ['src/ruby',
                            'test/distrib/ruby']


def _filter_whitelist(whitelist, triggers):
  """
  Removes triggers from whitelist
  :param whitelist: list to remove values from
  :param triggers: list of values to remove from whitelist
  :return: filtered whitelist
  """
  filtered_whitelist = list(whitelist)
  for trigger in triggers:
    if trigger in filtered_whitelist:
      filtered_whitelist.remove(trigger)
    else:
      """
      If the trigger is not found in the whitelist, then there is likely
      a mistake in the whitelist or trigger list, which needs to be addressed
      to not wrongly skip tests
      """
      print("ERROR: '%s' trigger not in whitelist. Please fix this!" % trigger)
  return filtered_whitelist


def _get_changed_files():
  """
  Get list of changed files between current branch and base of target merge branch
  """
  # git fetch might need to be called on Jenkins slave
  # todo(mattkwong): remove or uncomment below after seeing if Jenkins needs this
  # call(['git', 'fetch'])
  # this also collects files that are changed in the repo but not updated in the branch
  # todo(mattkwong): change this to only collect changes files compared to base and not hardcode branch
  return check_output(["git", "diff", "--name-only", "..origin/master"]).splitlines()


def _can_skip_tests(file_names, starts_with_whitelist=[], ends_with_whitelist=[]):
  """
  Determines if tests are skippable based on if all file names do not match
  any begin or end triggers
  :param file_names: list of changed files generated by _get_changed_files()
  :param starts_with_triggers: tuple of strings to match with beginning of file names
  :param ends_with_triggers: tuple of strings to match with end of file names
  :return: safe to skip tests
  """
  # convert lists to tuple to pass into str.startswith() and str.endswith()
  starts_with_whitelist = tuple(starts_with_whitelist)
  ends_with_whitelist = tuple(ends_with_whitelist)
  print (starts_with_whitelist)
  for file_name in file_names:
    if starts_with_whitelist and not file_name.startswith(starts_with_whitelist) and \
       ends_with_whitelist and not file_name.endswith(ends_with_whitelist):
         return False
  return True


def _remove_irrelevant_tests(tests, tag):
  """
  Filters out tests by config or language
  :param tests: list of all tests generated by run_tests_matrix.py
  :param tag: string representing language or config to filter - "_(language)_" or "_(config)"
  :return: list of relevant tests
  """
  # todo(mattkwong): find a more reliable way to filter tests - don't use shortname
  return [test for test in tests if not tag in test.shortname]


def filter_tests(tests):
  """
  Filters out tests that are safe to ignore
  :param tests: list of all tests generated by run_tests_matrix.py
  :return: list of relevant tests
  """
  print("Finding file differences between grpc:master repo and pull request...")
  changed_files = _get_changed_files()
  for changed_file in changed_files:
    print(changed_file)

  changed_files = ['src/ruby/dgf']

  # Filter core tests
  skip_core = _can_skip_tests(changed_files,
                              starts_with_whitelist=_filter_whitelist(starts_with_whitelist, core_starts_with_triggers),
                              ends_with_whitelist=ends_with_whitelist)
  if skip_core:
    tests = _remove_irrelevant_tests(tests, '_c_')

  # Filter c++ tests
  skip_cpp = _can_skip_tests(changed_files,
                             starts_with_whitelist=_filter_whitelist(starts_with_whitelist, cpp_starts_with_triggers),
                             ends_with_whitelist=ends_with_whitelist)
  if skip_cpp:
    tests = _remove_irrelevant_tests(tests, '_cpp_')

  # Tsan, msan, and asan tests skipped if core and c++ are skipped
  if skip_core and skip_cpp:
    tests = _remove_irrelevant_tests(tests, '_tsan')
    tests = _remove_irrelevant_tests(tests, '_msan')
    tests = _remove_irrelevant_tests(tests, '_asan')

  # Filter c# tests
  skip_csharp = _can_skip_tests(changed_files,
                                starts_with_whitelist=_filter_whitelist(starts_with_whitelist, csharp_starts_with_triggers),
                                ends_with_whitelist=ends_with_whitelist)
  if skip_csharp:
    tests = _remove_irrelevant_tests(tests, '_csharp_')

  # Filter node tests
  skip_node = _can_skip_tests(changed_files,
                              starts_with_whitelist=_filter_whitelist(starts_with_whitelist, node_starts_with_triggers),
                              ends_with_whitelist=ends_with_whitelist)
  if skip_node:
    tests = _remove_irrelevant_tests(tests, '_node_')

  # Filter objc tests
  skip_objc = _can_skip_tests(changed_files,
                              starts_with_whitelist=_filter_whitelist(starts_with_whitelist, objc_starts_with_triggers),
                              ends_with_whitelist=ends_with_whitelist)
  if skip_objc:
    tests = _remove_irrelevant_tests(tests, '_objc_')

  # Filter php tests
  skip_php = _can_skip_tests(changed_files,
                             starts_with_whitelist=_filter_whitelist(starts_with_whitelist, php_starts_with_triggers),
                             ends_with_whitelist=ends_with_whitelist)
  if skip_php:
    tests = _remove_irrelevant_tests(tests, '_php_')

  # Filter python tests
  skip_python = _can_skip_tests(changed_files,
                                starts_with_whitelist=_filter_whitelist(starts_with_whitelist, python_starts_with_triggers),
                                ends_with_whitelist=ends_with_whitelist)
  if skip_python:
    tests = _remove_irrelevant_tests(tests, '_python_')

  # Filter ruby tests
  skip_ruby = _can_skip_tests(changed_files,
                              starts_with_whitelist=_filter_whitelist(starts_with_whitelist, ruby_starts_with_triggers),
                              ends_with_whitelist=ends_with_whitelist)
  if skip_ruby:
    tests = _remove_irrelevant_tests(tests, '_ruby_')

  return tests
