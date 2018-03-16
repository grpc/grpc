#!/usr/bin/env python
# Copyright 2017 gRPC authors.
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
"""Uploads RBE results to BigQuery"""

import argparse
import os
import json
import sys
import urllib2
import uuid

gcp_utils_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '../../gcp/utils'))
sys.path.append(gcp_utils_dir)
import big_query_utils

_DATASET_ID = 'jenkins_test_results'
_DESCRIPTION = 'Test results from master RBE builds on Kokoro'
# 90 days in milliseconds
_EXPIRATION_MS = 90 * 24 * 60 * 60 * 1000
_PARTITION_TYPE = 'DAY'
_PROJECT_ID = 'grpc-testing'
_RESULTS_SCHEMA = [
    ('job_name', 'STRING', 'Name of Kokoro job'),
    ('build_id', 'INTEGER', 'Build ID of Kokoro job'),
    ('build_url', 'STRING', 'URL of Kokoro build'),
    ('test_target', 'STRING', 'Bazel target path'),
    ('result', 'STRING', 'Test or build result'),
    ('type', 'STRING', 'Type of Bazel target'),
    ('language', 'STRING', 'Language of target'),
    ('timestamp', 'TIMESTAMP', 'Timestamp of test run'),
    ('size', 'STRING', 'Size of Bazel target'),
]
_TABLE_ID = 'rbe_test_results'


def _fill_missing_fields(target):
    """Inserts 'N/A' to missing expected fields of Bazel target

  Args:
      target: A dictionary of a Bazel target's ResultStore data 
  """
    if 'type' not in target['targetAttributes']:
        target['targetAttributes']['type'] = 'N/A'
    if 'language' not in target['targetAttributes']:
        target['targetAttributes']['language'] = 'N/A'
    if 'testAttributes' not in target:
        target['testAttributes'] = {'size': 'N/A'}
    return target


def _get_api_key():
    """Returns string with API key to access ResultStore.
	Intended to be used in Kokoro envrionment."""
    api_key_directory = os.getenv('KOKORO_GFILE_DIR')
    api_key_file = os.path.join(api_key_directory, 'resultstore_api_key')
    assert os.path.isfile(api_key_file), 'Must add --api_key arg if not on ' \
     'Kokoro or Kokoro envrionment is not set up properly.'
    with open(api_key_file, 'r') as f:
        return f.read().replace('\n', '')


def _get_invocation_id():
    """Returns String of Bazel invocation ID. Intended to be used in
	Kokoro envirionment."""
    bazel_id_directory = os.getenv('KOKORO_ARTIFACTS_DIR')
    bazel_id_file = os.path.join(bazel_id_directory, 'bazel_invocation_ids')
    assert os.path.isfile(bazel_id_file), 'bazel_invocation_ids file, written ' \
     'by bazel_wrapper.py, expected but not found.'
    with open(bazel_id_file, 'r') as f:
        return f.read().replace('\n', '')


def _upload_results_to_bq(rows):
    """Upload test results to a BQ table.

  Args:
      rows: A list of dictionaries containing data for each row to insert
  """
    bq = big_query_utils.create_big_query()
    big_query_utils.create_partitioned_table(
        bq,
        _PROJECT_ID,
        _DATASET_ID,
        _TABLE_ID,
        _RESULTS_SCHEMA,
        _DESCRIPTION,
        partition_type=_PARTITION_TYPE,
        expiration_ms=_EXPIRATION_MS)

    max_retries = 3
    for attempt in range(max_retries):
        if big_query_utils.insert_rows(bq, _PROJECT_ID, _DATASET_ID, _TABLE_ID,
                                       rows):
            break
        else:
            if attempt < max_retries - 1:
                print('Error uploading result to bigquery, will retry.')
            else:
                print(
                    'Error uploading result to bigquery, all attempts failed.')
                sys.exit(1)


if __name__ == "__main__":
    # Arguments are necessary if running in a non-Kokoro envrionment.
    argp = argparse.ArgumentParser(description='Upload RBE results.')
    argp.add_argument('--api_key', default='', type=str)
    argp.add_argument('--invocation_id', default='', type=str)
    args = argp.parse_args()

    api_key = args.api_key or _get_api_key()
    invocation_id = args.invocation_id or _get_invocation_id()

    req = urllib2.Request(
        url='https://resultstore.googleapis.com/v2/invocations/%s/targets?key=%s'
        % (invocation_id, api_key),
        headers={
            'Content-Type': 'application/json'
        })

    results = json.loads(urllib2.urlopen(req).read())
    bq_rows = []
    for target in map(_fill_missing_fields, results['targets']):
        bq_rows.append({
            'insertId': str(uuid.uuid4()),
            'json': {
                'build_id':
                os.getenv('KOKORO_BUILD_NUMBER'),
                'build_url':
                'https://sponge.corp.google.com/invocation?id=%s' %
                os.getenv('KOKORO_BUILD_ID'),
                'job_name':
                os.getenv('KOKORO_JOB_NAME'),
                'test_target':
                target['id']['targetId'],
                'result':
                target['statusAttributes']['status'],
                'type':
                target['targetAttributes']['type'],
                'language':
                target['targetAttributes']['language'],
                'timestamp':
                target['timing']['startTime'],
                'size':
                target['testAttributes']['size']
            }
        })

    _upload_results_to_bq(bq_rows)
