#!/usr/bin/env python
# Copyright 2016, Google Inc.
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

"""Tool to get build statistics from Jenkins and upload to BigQuery."""

from __future__ import print_function

import argparse
import jenkinsapi
from jenkinsapi.custom_exceptions import JenkinsAPIException
from jenkinsapi.jenkins import Jenkins
import json
import os
import re
import sys
import urllib


gcp_utils_dir = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../gcp/utils'))
sys.path.append(gcp_utils_dir)
import big_query_utils


_PROJECT_ID = 'grpc-testing'
_HAS_MATRIX = True
_BUILDS = {'gRPC_interop_master': not _HAS_MATRIX,
           'gRPC_master_linux': not _HAS_MATRIX,
           'gRPC_master_macos': not _HAS_MATRIX,
           'gRPC_master_windows': not _HAS_MATRIX,
           'gRPC_performance_master': not _HAS_MATRIX,
           'gRPC_portability_master_linux': not _HAS_MATRIX,
           'gRPC_portability_master_windows': not _HAS_MATRIX,
           'gRPC_master_asanitizer_c': not _HAS_MATRIX,
           'gRPC_master_asanitizer_cpp': not _HAS_MATRIX,
           'gRPC_master_msan_c': not _HAS_MATRIX,
           'gRPC_master_tsanitizer_c': not _HAS_MATRIX,
           'gRPC_master_tsan_cpp': not _HAS_MATRIX,
           'gRPC_interop_pull_requests': not _HAS_MATRIX,
           'gRPC_performance_pull_requests': not _HAS_MATRIX,
           'gRPC_portability_pull_requests_linux': not _HAS_MATRIX,
           'gRPC_portability_pr_win': not _HAS_MATRIX,
           'gRPC_pull_requests_linux': not _HAS_MATRIX,
           'gRPC_pull_requests_macos': not _HAS_MATRIX,
           'gRPC_pr_win': not _HAS_MATRIX,
           'gRPC_pull_requests_asan_c': not _HAS_MATRIX,
           'gRPC_pull_requests_asan_cpp': not _HAS_MATRIX,
           'gRPC_pull_requests_msan_c': not _HAS_MATRIX,
           'gRPC_pull_requests_tsan_c': not _HAS_MATRIX,
           'gRPC_pull_requests_tsan_cpp': not _HAS_MATRIX,
}
_URL_BASE = 'https://grpc-testing.appspot.com/job'

# This is a dynamic list where known and active issues should be added. 
# Fixed ones should be removed.
# Also try not to add multiple messages from the same failure.
_KNOWN_ERRORS = [
    'Failed to build workspace Tests with scheme AllTests',
    'Build timed out',
    'TIMEOUT: tools/run_tests/pre_build_node.sh',
    'TIMEOUT: tools/run_tests/pre_build_ruby.sh',
    'FATAL: Unable to produce a script file',
    'FAILED: build_docker_c\+\+',
    'cannot find package \"cloud.google.com/go/compute/metadata\"',
    'LLVM ERROR: IO failure on output stream.',
    'MSBUILD : error MSB1009: Project file does not exist.',
    'fatal: git fetch_pack: expected ACK/NAK',
    'Failed to fetch from http://github.com/grpc/grpc.git',
    ('hudson.remoting.RemotingSystemException: java.io.IOException: '
     'Backing channel is disconnected.'),
    'hudson.remoting.ChannelClosedException',
    'Could not initialize class hudson.Util',
    'Too many open files in system',
    'FAILED: bins/tsan/qps_openloop_test GRPC_POLL_STRATEGY=epoll',
    'FAILED: bins/tsan/qps_openloop_test GRPC_POLL_STRATEGY=legacy',
    'FAILED: bins/tsan/qps_openloop_test GRPC_POLL_STRATEGY=poll',
    ('tests.bins/asan/h2_proxy_test streaming_error_response '
     'GRPC_POLL_STRATEGY=legacy'),
]
_NO_REPORT_FILES_FOUND_ERROR = 'No test report files were found. Configuration error?'
_UNKNOWN_ERROR = 'Unknown error'
_DATASET_ID = 'build_statistics'


def _scrape_for_known_errors(html):
  error_list = []
  known_error_count = 0
  for known_error in _KNOWN_ERRORS:
    errors = re.findall(known_error, html)
    this_error_count = len(errors)
    if this_error_count > 0: 
      known_error_count += this_error_count
      error_list.append({'description': known_error,
                         'count': this_error_count})
      print('====> %d failures due to %s' % (this_error_count, known_error))
  return error_list, known_error_count


def _no_report_files_found(html):
  return _NO_REPORT_FILES_FOUND_ERROR in html


def _get_last_processed_buildnumber(build_name):
  query = 'SELECT max(build_number) FROM [%s:%s.%s];' % (
      _PROJECT_ID, _DATASET_ID, build_name)
  query_job = big_query_utils.sync_query_job(bq, _PROJECT_ID, query)
  page = bq.jobs().getQueryResults(
      pageToken=None,
      **query_job['jobReference']).execute(num_retries=3)
  if page['rows'][0]['f'][0]['v']:
    return int(page['rows'][0]['f'][0]['v'])
  return 0


def _process_matrix(build, url_base):
  matrix_list = []
  for matrix in build.get_matrix_runs():
    matrix_str = re.match('.*\\xc2\\xbb ((?:[^,]+,?)+) #.*', 
                          matrix.name).groups()[0]
    matrix_tuple = matrix_str.split(',')
    json_url = '%s/config=%s,language=%s,platform=%s/testReport/api/json' % (
        url_base, matrix_tuple[0], matrix_tuple[1], matrix_tuple[2])
    console_url = '%s/config=%s,language=%s,platform=%s/consoleFull' % (
        url_base, matrix_tuple[0], matrix_tuple[1], matrix_tuple[2])
    matrix_dict = {'name': matrix_str,
                   'duration': matrix.get_duration().total_seconds()}
    matrix_dict.update(_process_build(json_url, console_url))
    matrix_list.append(matrix_dict)

  return matrix_list 


def _process_build(json_url, console_url):
  build_result = {}
  error_list = []
  try:
    html = urllib.urlopen(json_url).read()
    test_result = json.loads(html)
    print('====> Parsing result from %s' % json_url)
    failure_count = test_result['failCount']
    build_result['pass_count'] = test_result['passCount']
    build_result['failure_count'] = failure_count
    build_result['no_report_files_found'] = _no_report_files_found(html)
    if failure_count > 0:
      error_list, known_error_count = _scrape_for_known_errors(html)
      unknown_error_count = failure_count - known_error_count
      # This can happen if the same error occurs multiple times in one test.
      if failure_count < known_error_count:
        print('====> Some errors are duplicates.')
        unknown_error_count = 0
      error_list.append({'description': _UNKNOWN_ERROR, 
                         'count': unknown_error_count})
  except Exception as e:
    print('====> Got exception for %s: %s.' % (json_url, str(e)))   
    print('====> Parsing errors from %s.' % console_url)
    html = urllib.urlopen(console_url).read()
    build_result['pass_count'] = 0  
    build_result['failure_count'] = 1
    error_list, _ = _scrape_for_known_errors(html)
    if error_list:
      error_list.append({'description': _UNKNOWN_ERROR, 'count': 0})
    else:
      error_list.append({'description': _UNKNOWN_ERROR, 'count': 1})
 
  if error_list:
    build_result['error'] = error_list

  return build_result 


# parse command line
argp = argparse.ArgumentParser(description='Get build statistics.')
argp.add_argument('-u', '--username', default='jenkins')
argp.add_argument('-b', '--builds', 
                  choices=['all'] + sorted(_BUILDS.keys()),
                  nargs='+',
                  default=['all'])
args = argp.parse_args()

J = Jenkins('https://grpc-testing.appspot.com', args.username, 'apiToken')
bq = big_query_utils.create_big_query()

for build_name in _BUILDS.keys() if 'all' in args.builds else args.builds:
  print('====> Build: %s' % build_name)
  # Since get_last_completed_build() always fails due to malformatted string
  # error, we use get_build_metadata() instead.
  job = None
  try:
    job = J[build_name]
  except Exception as e:
    print('====> Failed to get build %s: %s.' % (build_name, str(e)))
    continue
  last_processed_build_number = _get_last_processed_buildnumber(build_name)
  last_complete_build_number = job.get_last_completed_buildnumber()
  # To avoid processing all builds for a project never looked at. In this case,
  # only examine 10 latest builds.
  starting_build_number = max(last_processed_build_number+1, 
                              last_complete_build_number-9)
  for build_number in xrange(starting_build_number, 
                             last_complete_build_number+1):
    print('====> Processing %s build %d.' % (build_name, build_number))
    build = None
    try:
      build = job.get_build_metadata(build_number)
    except KeyError:
      print('====> Build %s is missing. Skip.' % build_number)
      continue
    build_result = {'build_number': build_number, 
                    'timestamp': str(build.get_timestamp())}
    url_base = json_url = '%s/%s/%d' % (_URL_BASE, build_name, build_number)
    if _BUILDS[build_name]:  # The build has matrix, such as gRPC_master.
      build_result['matrix'] = _process_matrix(build, url_base)
    else:
      json_url = '%s/testReport/api/json' % url_base
      console_url = '%s/consoleFull' % url_base
      build_result['duration'] = build.get_duration().total_seconds()
      build_result.update(_process_build(json_url, console_url))
    rows = [big_query_utils.make_row(build_number, build_result)]
    if not big_query_utils.insert_rows(bq, _PROJECT_ID, _DATASET_ID, build_name, 
                                       rows):
      print('====> Error uploading result to bigquery.')
      sys.exit(1)

