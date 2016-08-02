#!/usr/bin/env python2.7
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

# Uploads performance benchmark result file to bigquery.

import argparse
import calendar
import json
import os
import sys
import time
import uuid


gcp_utils_dir = os.path.abspath(os.path.join(
    os.path.dirname(__file__), '../../gcp/utils'))
sys.path.append(gcp_utils_dir)
import big_query_utils


_PROJECT_ID='grpc-testing'


def _upload_netperf_latency_csv_to_bigquery(dataset_id, table_id, result_file):
  with open(result_file, 'r') as f:
    (col1, col2, col3) = f.read().split(',')
    latency50 = float(col1.strip()) * 1000
    latency90 = float(col2.strip()) * 1000
    latency99 = float(col3.strip()) * 1000

    scenario_result = {
        'scenario': {
          'name': 'netperf_tcp_rr'
        },
        'summary': {
          'latency50': latency50,
          'latency90': latency90,
          'latency99': latency99
        }
    }

  bq = big_query_utils.create_big_query()
  _create_results_table(bq, dataset_id, table_id)

  if not _insert_result(bq, dataset_id, table_id, scenario_result, flatten=False):
    print 'Error uploading result to bigquery.'
    sys.exit(1)


def _upload_scenario_result_to_bigquery(dataset_id, table_id, result_file):
  with open(result_file, 'r') as f:
    scenario_result = json.loads(f.read())

  bq = big_query_utils.create_big_query()
  _create_results_table(bq, dataset_id, table_id)

  if not _insert_result(bq, dataset_id, table_id, scenario_result):
    print 'Error uploading result to bigquery.'
    sys.exit(1)


def _insert_result(bq, dataset_id, table_id, scenario_result, flatten=True):
  if flatten:
    _flatten_result_inplace(scenario_result)
  _populate_metadata_inplace(scenario_result)
  row = big_query_utils.make_row(str(uuid.uuid4()), scenario_result)
  return big_query_utils.insert_rows(bq,
                                     _PROJECT_ID,
                                     dataset_id,
                                     table_id,
                                     [row])


def _create_results_table(bq, dataset_id, table_id):
  with open(os.path.dirname(__file__) + '/scenario_result_schema.json', 'r') as f:
    table_schema = json.loads(f.read())
  desc = 'Results of performance benchmarks.'
  return big_query_utils.create_table2(bq, _PROJECT_ID, dataset_id,
                               table_id, table_schema, desc)


def _flatten_result_inplace(scenario_result):
  """Bigquery is not really great for handling deeply nested data
  and repeated fields. To maintain values of some fields while keeping
  the schema relatively simple, we artificially leave some of the fields
  as JSON strings.
  """
  scenario_result['scenario']['clientConfig'] = json.dumps(scenario_result['scenario']['clientConfig'])
  scenario_result['scenario']['serverConfig'] = json.dumps(scenario_result['scenario']['serverConfig'])
  scenario_result['latencies'] = json.dumps(scenario_result['latencies'])
  for stats in scenario_result['clientStats']:
    stats['latencies'] = json.dumps(stats['latencies'])
  scenario_result['serverCores'] = json.dumps(scenario_result['serverCores'])
  scenario_result['clientSuccess'] = json.dumps(scenario_result['clientSuccess'])
  scenario_result['serverSuccess'] = json.dumps(scenario_result['serverSuccess'])


def _populate_metadata_inplace(scenario_result):
  """Populates metadata based on environment variables set by Jenkins."""
  # NOTE: Grabbing the Jenkins environment variables will only work if the
  # driver is running locally on the same machine where Jenkins has started
  # the job. For our setup, this is currently the case, so just assume that.
  build_number = os.getenv('BUILD_NUMBER')
  build_url = os.getenv('BUILD_URL')
  job_name = os.getenv('JOB_NAME')
  git_commit = os.getenv('GIT_COMMIT')
  # actual commit is the actual head of PR that is getting tested
  git_actual_commit = os.getenv('ghprbActualCommit')

  utc_timestamp = str(calendar.timegm(time.gmtime()))
  metadata = {'created': utc_timestamp}

  if build_number:
    metadata['buildNumber'] = build_number
  if build_url:
    metadata['buildUrl'] = build_url
  if job_name:
    metadata['jobName'] = job_name
  if git_commit:
    metadata['gitCommit'] = git_commit
  if git_actual_commit:
    metadata['gitActualCommit'] = git_actual_commit

  scenario_result['metadata'] = metadata


argp = argparse.ArgumentParser(description='Upload result to big query.')
argp.add_argument('--bq_result_table', required=True, default=None, type=str,
                  help='Bigquery "dataset.table" to upload results to.')
argp.add_argument('--file_to_upload', default='scenario_result.json', type=str,
                  help='Report file to upload.')
argp.add_argument('--file_format',
                  choices=['scenario_result','netperf_latency_csv'],
                  default='scenario_result',
                  help='Format of the file to upload.')

args = argp.parse_args()

dataset_id, table_id = args.bq_result_table.split('.', 2)

if args.file_format == 'netperf_latency_csv':
  _upload_netperf_latency_csv_to_bigquery(dataset_id, table_id, args.file_to_upload)
else:
  _upload_scenario_result_to_bigquery(dataset_id, table_id, args.file_to_upload)
print 'Successfully uploaded %s to BigQuery.\n' % args.file_to_upload
