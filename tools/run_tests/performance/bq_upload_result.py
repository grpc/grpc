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

# Uploads performance benchmark result file to bigquery.

from __future__ import print_function

import argparse
import calendar
import json
import os
import sys
import time
import uuid
import massage_qps_stats

gcp_utils_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '../../gcp/utils'))
sys.path.append(gcp_utils_dir)
import big_query_utils

_PROJECT_ID = 'grpc-testing'


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

    if not _insert_result(
            bq, dataset_id, table_id, scenario_result, flatten=False):
        print('Error uploading result to bigquery.')
        sys.exit(1)


def _upload_scenario_result_to_bigquery(dataset_id, table_id, result_file):
    with open(result_file, 'r') as f:
        scenario_result = json.loads(f.read())

    bq = big_query_utils.create_big_query()
    _create_results_table(bq, dataset_id, table_id)

    if not _insert_result(bq, dataset_id, table_id, scenario_result):
        print('Error uploading result to bigquery.')
        sys.exit(1)


def _insert_result(bq, dataset_id, table_id, scenario_result, flatten=True):
    if flatten:
        _flatten_result_inplace(scenario_result)
    _populate_metadata_inplace(scenario_result)
    row = big_query_utils.make_row(str(uuid.uuid4()), scenario_result)
    return big_query_utils.insert_rows(bq, _PROJECT_ID, dataset_id, table_id,
                                       [row])


def _create_results_table(bq, dataset_id, table_id):
    with open(os.path.dirname(__file__) + '/scenario_result_schema.json',
              'r') as f:
        table_schema = json.loads(f.read())
    desc = 'Results of performance benchmarks.'
    return big_query_utils.create_table2(bq, _PROJECT_ID, dataset_id, table_id,
                                         table_schema, desc)


def _flatten_result_inplace(scenario_result):
    """Bigquery is not really great for handling deeply nested data
  and repeated fields. To maintain values of some fields while keeping
  the schema relatively simple, we artificially leave some of the fields
  as JSON strings.
  """
    scenario_result['scenario']['clientConfig'] = json.dumps(
        scenario_result['scenario']['clientConfig'])
    scenario_result['scenario']['serverConfig'] = json.dumps(
        scenario_result['scenario']['serverConfig'])
    scenario_result['latencies'] = json.dumps(scenario_result['latencies'])
    scenario_result['serverCpuStats'] = []
    for stats in scenario_result['serverStats']:
        scenario_result['serverCpuStats'].append(dict())
        scenario_result['serverCpuStats'][-1]['totalCpuTime'] = stats.pop(
            'totalCpuTime', None)
        scenario_result['serverCpuStats'][-1]['idleCpuTime'] = stats.pop(
            'idleCpuTime', None)
    for stats in scenario_result['clientStats']:
        stats['latencies'] = json.dumps(stats['latencies'])
        stats.pop('requestResults', None)
    scenario_result['serverCores'] = json.dumps(scenario_result['serverCores'])
    scenario_result['clientSuccess'] = json.dumps(
        scenario_result['clientSuccess'])
    scenario_result['serverSuccess'] = json.dumps(
        scenario_result['serverSuccess'])
    scenario_result['requestResults'] = json.dumps(
        scenario_result.get('requestResults', []))
    scenario_result['serverCpuUsage'] = scenario_result['summary'].pop(
        'serverCpuUsage', None)
    scenario_result['summary'].pop('successfulRequestsPerSecond', None)
    scenario_result['summary'].pop('failedRequestsPerSecond', None)
    massage_qps_stats.massage_qps_stats(scenario_result)


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
argp.add_argument(
    '--bq_result_table',
    required=True,
    default=None,
    type=str,
    help='Bigquery "dataset.table" to upload results to.')
argp.add_argument(
    '--file_to_upload',
    default='scenario_result.json',
    type=str,
    help='Report file to upload.')
argp.add_argument(
    '--file_format',
    choices=['scenario_result', 'netperf_latency_csv'],
    default='scenario_result',
    help='Format of the file to upload.')

args = argp.parse_args()

dataset_id, table_id = args.bq_result_table.split('.', 2)

if args.file_format == 'netperf_latency_csv':
    _upload_netperf_latency_csv_to_bigquery(dataset_id, table_id,
                                            args.file_to_upload)
else:
    _upload_scenario_result_to_bigquery(dataset_id, table_id,
                                        args.file_to_upload)
print('Successfully uploaded %s to BigQuery.\n' % args.file_to_upload)
