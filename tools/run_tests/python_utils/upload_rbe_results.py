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
    ('test_case', 'STRING', 'Name of test case'),
    ('result', 'STRING', 'Test or build result'),
    ('timestamp', 'TIMESTAMP', 'Timestamp of test run'),
    ('duration', 'FLOAT', 'Duration of the test run'),
]
_TABLE_ID = 'rbe_test_results'


def _get_api_key():
    """Returns string with API key to access ResultStore.
	Intended to be used in Kokoro environment."""
    api_key_directory = os.getenv('KOKORO_GFILE_DIR')
    api_key_file = os.path.join(api_key_directory, 'resultstore_api_key')
    assert os.path.isfile(api_key_file), 'Must add --api_key arg if not on ' \
     'Kokoro or Kokoro envrionment is not set up properly.'
    with open(api_key_file, 'r') as f:
        return f.read().replace('\n', '')


def _get_invocation_id():
    """Returns String of Bazel invocation ID. Intended to be used in
	Kokoro environment."""
    bazel_id_directory = os.getenv('KOKORO_ARTIFACTS_DIR')
    bazel_id_file = os.path.join(bazel_id_directory, 'bazel_invocation_ids')
    assert os.path.isfile(bazel_id_file), 'bazel_invocation_ids file, written ' \
     'by RBE initialization script, expected but not found.'
    with open(bazel_id_file, 'r') as f:
        return f.read().replace('\n', '')


def _parse_test_duration(duration_str):
    """Parse test duration string in '123.567s' format"""
    try:
        if duration_str.endswith('s'):
            duration_str = duration_str[:-1]
        return float(duration_str)
    except:
        return None


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


def _get_resultstore_data(api_key, invocation_id):
    """Returns dictionary of test results by querying ResultStore API.
  Args:
      api_key: String of ResultStore API key
      invocation_id: String of ResultStore invocation ID to results from 
  """
    all_actions = []
    page_token = ''
    # ResultStore's API returns data on a limited number of tests. When we exceed
    # that limit, the 'nextPageToken' field is included in the request to get
    # subsequent data, so keep requesting until 'nextPageToken' field is omitted.
    while True:
        req = urllib2.Request(
            url=
            'https://resultstore.googleapis.com/v2/invocations/%s/targets/-/configuredTargets/-/actions?key=%s&pageToken=%s'
            % (invocation_id, api_key, page_token),
            headers={
                'Content-Type': 'application/json'
            })
        results = json.loads(urllib2.urlopen(req).read())
        all_actions.extend(results['actions'])
        if 'nextPageToken' not in results:
            break
        page_token = results['nextPageToken']
    return all_actions


if __name__ == "__main__":
    # Arguments are necessary if running in a non-Kokoro environment.
    argp = argparse.ArgumentParser(description='Upload RBE results.')
    argp.add_argument('--api_key', default='', type=str)
    argp.add_argument('--invocation_id', default='', type=str)
    args = argp.parse_args()

    api_key = args.api_key or _get_api_key()
    invocation_id = args.invocation_id or _get_invocation_id()
    resultstore_actions = _get_resultstore_data(api_key, invocation_id)

    bq_rows = []
    for index, action in enumerate(resultstore_actions):
        # Filter out non-test related data, such as build results.
        if 'testAction' not in action:
            continue
        # Some test results contain the fileProcessingErrors field, which indicates
        # an issue with parsing results individual test cases.
        if 'fileProcessingErrors' in action:
            test_cases = [{
                'testCase': {
                    'caseName': str(action['id']['actionId']),
                }
            }]
        # Test timeouts have a different dictionary structure compared to pass and
        # fail results.
        elif action['statusAttributes']['status'] == 'TIMED_OUT':
            test_cases = [{
                'testCase': {
                    'caseName': str(action['id']['actionId']),
                    'timedOut': True
                }
            }]
        # When RBE believes its infrastructure is failing, it will abort and
        # mark running tests as UNKNOWN. These infrastructure failures may be
        # related to our tests, so we should investigate if specific tests are
        # repeatedly being marked as UNKNOWN.
        elif action['statusAttributes']['status'] == 'UNKNOWN':
            test_cases = [{
                'testCase': {
                    'caseName': str(action['id']['actionId']),
                    'unknown': True
                }
            }]
            # Take the timestamp from the previous action, which should be
            # a close approximation.
            action['timing'] = {
                'startTime':
                resultstore_actions[index - 1]['timing']['startTime']
            }
        else:
            test_cases = action['testAction']['testSuite']['tests'][0][
                'testSuite']['tests']
        for test_case in test_cases:
            if any(s in test_case['testCase'] for s in ['errors', 'failures']):
                result = 'FAILED'
            elif 'timedOut' in test_case['testCase']:
                result = 'TIMEOUT'
            elif 'unknown' in test_case['testCase']:
                result = 'UNKNOWN'
            else:
                result = 'PASSED'
            try:
                bq_rows.append({
                    'insertId': str(uuid.uuid4()),
                    'json': {
                        'job_name':
                        os.getenv('KOKORO_JOB_NAME'),
                        'build_id':
                        os.getenv('KOKORO_BUILD_NUMBER'),
                        'build_url':
                        'https://source.cloud.google.com/results/invocations/%s'
                        % invocation_id,
                        'test_target':
                        action['id']['targetId'],
                        'test_case':
                        test_case['testCase']['caseName'],
                        'result':
                        result,
                        'timestamp':
                        action['timing']['startTime'],
                        'duration':
                        _parse_test_duration(action['timing']['duration']),
                    }
                })
            except Exception as e:
                print('Failed to parse test result. Error: %s' % str(e))
                print(json.dumps(test_case, indent=4))
                bq_rows.append({
                    'insertId': str(uuid.uuid4()),
                    'json': {
                        'job_name':
                        os.getenv('KOKORO_JOB_NAME'),
                        'build_id':
                        os.getenv('KOKORO_BUILD_NUMBER'),
                        'build_url':
                        'https://source.cloud.google.com/results/invocations/%s'
                        % invocation_id,
                        'test_target':
                        action['id']['targetId'],
                        'test_case':
                        'N/A',
                        'result':
                        'UNPARSEABLE',
                        'timestamp':
                        'N/A',
                    }
                })

    # BigQuery sometimes fails with large uploads, so batch 1,000 rows at a time.
    for i in range((len(bq_rows) / 1000) + 1):
        _upload_results_to_bq(bq_rows[i * 1000:(i + 1) * 1000])
