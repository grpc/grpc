#!/usr/bin/env python3
# Copyright 2022 The gRPC Authors
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
"""Scrapes resultstore API"""

import argparse
import json
import os
import ssl
import sys
import urllib.error
import urllib.parse
import urllib.request
import uuid
import time
import datetime

gcp_utils_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '../../gcp/utils'))
sys.path.append(gcp_utils_dir)
import big_query_utils
import upload_rbe_results

_PROJECT_ID = '830293263384'  # "grpc-testing" cloud project

_DATASET_ID = 'jtattermusch_experiment'
_DESCRIPTION = 'Resultstore invocations from master RBE builds on Kokoro'
# 7 days
_EXPIRATION_MS = 7 * 24 * 60 * 60 * 1000
_PARTITION_TYPE = 'DAY'
_PROJECT_ID = 'grpc-testing'
_RESULTS_SCHEMA = [
    ('invocation_id', 'STRING', 'Bazel invocation ID'),
    ('invocation_status', 'STRING', 'Invocation status'),
    ('invocation_start_time', 'TIMESTAMP', 'Start time of invocation'),
    ('invocation_duration', 'FLOAT', 'Duration of the entire invocation'),
    ('invocation_property_kokoro_job_name', 'STRING', 'Originating kokoro job name.'),
    ('invocation_property_kokoro_build_number', 'STRING', 'Originating kokoro job build number.'),
    ('invocation_property_kokoro_resultstore_url', 'STRING', 'Resultstore URL of the originating kokoro job'),
    ('invocation_property_kokoro_sponge_url', 'STRING', 'Sponge URL of the originating kokoro job'),
    # note that KOKORO_GITHUB_COMMIT property is present for all master jobs but not for PR jobs
    ('invocation_property_kokoro_github_commit', 'STRING', 'Commit on which the originating kokoro job ran'),
    ('invocation_property_build_user', 'STRING', 'Username that started bazel invocation'),
    ('invocation_property_bazel_version', 'STRING', 'bazel version used'),
]
_TABLE_ID = 'resultstore_invocations_bazel_experiment_20220829_1'


def _get_api_key():
    """Returns string with API key to access ResultStore.
	Intended to be used in Kokoro environment."""
    api_key_directory = os.getenv('KOKORO_GFILE_DIR')
    api_key_file = os.path.join(api_key_directory, 'resultstore_api_key')
    assert os.path.isfile(api_key_file), 'Must add --api_key arg if not on ' \
     'Kokoro or Kokoro environment is not set up properly.'
    with open(api_key_file, 'r') as f:
        return f.read().replace('\n', '')


def _parse_test_duration(duration_str):
    """Parse test duration string in '123.567s' format"""
    try:
        if duration_str.endswith('s'):
            duration_str = duration_str[:-1]
        return float(duration_str)
    except:
        return None


def _invocation_extract_property(invocation, key, default):
    """Extract invocation property with given key"""

    # TODO(jtattermusch): strip \r characters from values
    for prop in invocation['properties']:
        if prop['key'] == key:
            return prop.get('value', default)
    return default


def _invocation_extract_info(invocation):
    """Extract info from invocation JSON."""
    converted={
        'invocation_id': invocation['id']['invocationId'],
        'invocation_status': invocation['statusAttributes']['status'],
        'invocation_start_time': invocation['timing'].get('startTime'),
        'invocation_duration': _parse_test_duration(invocation['timing'].get('duration')),
        'invocation_property_kokoro_job_name': _invocation_extract_property(invocation, 'KOKORO_JOB_NAME', ''),
        'invocation_property_kokoro_build_number': _invocation_extract_property(invocation, 'KOKORO_BUILD_NUMBER', ''),
        'invocation_property_kokoro_resultstore_url': _invocation_extract_property(invocation, 'KOKORO_RESULTSTORE_URL', ''),
        'invocation_property_kokoro_sponge_url': _invocation_extract_property(invocation, 'KOKORO_SPONGE_URL', ''),
        # note that KOKORO_GITHUB_COMMIT property is present for all master jobs but not for PR jobs
        'invocation_property_kokoro_github_commit': _invocation_extract_property(invocation, 'KOKORO_GITHUB_COMMIT', ''),
        'invocation_property_build_user': _invocation_extract_property(invocation, 'BUILD_USER', ''),
        'invocation_property_bazel_version': _invocation_extract_property(invocation, 'BAZEL_VERSION', ''),
    }
    return converted



def _get_resultstore_invocations(api_key, date_from, date_to):
    """Returns resulstore invocations by querying ResultStore API.
    Args:
        api_key: String of ResultStore API key
    """
    # invocation API schema https://github.com/googleapis/googleapis/blob/master/google/devtools/resultstore/v2/invocation.proto
    
    all_invocations = []
    page_token = ''
    while True:
        # query for all invocations that are bazel invocations after given timestamp.
        date_from_iso_with_z = date_from.strftime('%Y-%m-%dT%H:%M:%SZ')
        date_to_iso_with_z = date_to.strftime('%Y-%m-%dT%H:%M:%SZ')
        # query for bazel invocations in interval [date_from, date_to)
        query = 'invocation_attributes.labels:bazel%20timing.start_time>="' + date_from_iso_with_z + '"%20timing.start_time<"'+ date_to_iso_with_z + '"'
        
        req = urllib.request.Request(
            url=
            'https://resultstore.googleapis.com/v2/invocations:search?project_id=%s&pageToken=%s&key=%s&query=%s&fields=next_page_token,invocations.id,invocations.status_attributes,invocations.properties,invocations.timing' % (_PROJECT_ID, page_token, api_key, query),

            #'https://resultstore.googleapis.com/v2/invocations/%s?key=%s&fields=name,id,status_attributes,invocation_attributes,timing,properties,files'
            #% (invocation_id, api_key),

            headers={'Content-Type': 'application/json'})
        ctx_dict = {}
        if os.getenv("PYTHONHTTPSVERIFY") == "0":
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            ctx_dict = {"context": ctx}
        raw_resp = urllib.request.urlopen(req, **ctx_dict).read()
        decoded_resp = raw_resp.decode('utf-8', 'ignore')
        results = json.loads(decoded_resp)
        invocations_current_page = results.get('invocations', [])
        all_invocations.extend(invocations_current_page)
        print('collected page with %d invocations (%d total)' % (len(invocations_current_page), len(all_invocations)))
        if 'nextPageToken' not in results:
            break
        page_token = results['nextPageToken']
    
    return all_invocations


def _get_resultstore_actions_for_invocation(api_key, invocation_id):
    """Returns list of test results for given invocation by querying ResultStore API.
  Args:
      api_key: String of ResultStore API key
      invocation_id: String of ResultStore invocation ID to results from 
  """
    # Will return all esultstore Actions object for given invocation.
    # See https://github.com/googleapis/googleapis/blob/master/google/devtools/resultstore/v2/action.proto
    # for the defintion of Action and TestAction.
    all_actions = []
    page_token = ''
    # ResultStore's API returns data on a limited number of tests. When we exceed
    # that limit, the 'nextPageToken' field is included in the request to get
    # subsequent data, so keep requesting until 'nextPageToken' field is omitted.
    while True:
        req = urllib.request.Request(
            url=
            'https://resultstore.googleapis.com/v2/invocations/%s/targets/-/configuredTargets/-/actions?key=%s&pageToken=%s&fields=next_page_token,actions.id,actions.status_attributes,actions.timing,actions.test_action'
            % (invocation_id, api_key, page_token),
            headers={'Content-Type': 'application/json'})
        ctx_dict = {}
        if os.getenv("PYTHONHTTPSVERIFY") == "0":
            ctx = ssl.create_default_context()
            ctx.check_hostname = False
            ctx.verify_mode = ssl.CERT_NONE
            ctx_dict = {"context": ctx}
        raw_resp = urllib.request.urlopen(req, **ctx_dict).read()
        decoded_resp = raw_resp if isinstance(
            raw_resp, str) else raw_resp.decode('utf-8', 'ignore')
        results = json.loads(decoded_resp)
        all_actions.extend(results['actions'])
        if 'nextPageToken' not in results:
            break
        page_token = results['nextPageToken']
    return all_actions


def _upload_invocations_to_bq(rows):
    """Upload invocations to a BQ table.

  Args:
      rows: A list of dictionaries containing data for each row to insert
  """
    table_id = _TABLE_ID + '_' + str(int(time.time() * 1000))

    bq = big_query_utils.create_big_query()
    big_query_utils.create_partitioned_table(bq,
                                             _PROJECT_ID,
                                             _DATASET_ID,
                                             table_id,
                                             _RESULTS_SCHEMA,
                                             _DESCRIPTION,
                                             partition_type=_PARTITION_TYPE,
                                             expiration_ms=_EXPIRATION_MS)

    max_retries = 3
    for attempt in range(max_retries):
        if big_query_utils.insert_rows(bq, _PROJECT_ID, _DATASET_ID, table_id,
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
    argp = argparse.ArgumentParser(
        description=
        'Fetches results of RBE invocations and uploads them to BigQuery table(s).'
    )
    argp.add_argument('--api_key',
                      default='',
                      type=str,
                      help='The API key to read from ResultStore API')
    argp.add_argument('--invocation_dump_file',
                      default=None,
                      type=str,
                      help='Dump JSON data with invocations.')
    argp.add_argument('--action_dump_dir',
                      default=None,
                      type=str,
                      help='Dump JSON for test actions for all invocations.')
    argp.add_argument('--date_from',
                      required=True,  
                      type=str,
                      help='Collect resultstore invocations starting from a given day (YYYY-mm-dd format).')
    argp.add_argument('--date_to',
                      default='2050-01-01',
                      type=str,
                      help='Collect resultstore invocations starting from a given day (YYYY-mm-dd format).')
    # argp.add_argument('--skip_upload',
    #                   default=False,
    #                   action='store_const',
    #                   const=True,
    #                   help='Skip uploading to bigquery')
    args = argp.parse_args()

    api_key = args.api_key or _get_api_key()
    date_from = datetime.datetime.strptime(args.date_from, '%Y-%m-%d')
    date_to = datetime.datetime.strptime(args.date_to, '%Y-%m-%d')
    print('Will query for invocations in interval [%s, %s)' % (date_from, date_to))

    # get resultstore invocations since some date
    resultstore_invocations = _get_resultstore_invocations(api_key, date_from, date_to)

    # TODO: should we incorporate loader run ID?
    # identify individual runs of the loader tool, alternatively use str(uuid.uuid4())????
    loader_run_id = str(int(time.time() * 1000))

    converted = [_invocation_extract_info(inv) for inv in resultstore_invocations]

    # select invocations ids for which to get the details
    max_count = 1000000
    selected_invocations = []
    for inv in converted:
        if len(selected_invocations) >= max_count:
            continue
        if inv['invocation_status'] == 'BUILDING':
            # invocation still in progress, skip for now
            continue
        if not inv['invocation_property_kokoro_job_name'] or not inv['invocation_property_kokoro_job_name'].startswith('grpc/core/master'):
            # we only care about master jobs coming from CI for now
            continue
        selected_invocations.append(inv)

    # optionally dump the invocations to JSON file
    if args.invocation_dump_file:
        with open(args.invocation_dump_file, 'w') as f:
            json.dump(selected_invocations, f, indent=4, sort_keys=True)
        print('Dumped %d invocations to file %s' % (len(selected_invocations), args.invocation_dump_file))

    with open('actions_newline_delimited.json', 'w') as newline_delimited_f:

        # load actions for each of the selected invocations
        for selected_invocation in selected_invocations:
            # TODO: print index for each invocation being processed, URL, job_name

            invocation_id = selected_invocation['invocation_id']
            resultstore_actions = _get_resultstore_actions_for_invocation(api_key, invocation_id)
            print('Collected %d actions for invocation %s' % (len(resultstore_actions), invocation_id))
            
            # TODO: number the rows
            extra_metadata_columns = dict(selected_invocation)
            #extra_metadata_columns['_PARTITIONDATE'] = '2022-08-29'
            # TODO: massage the extra metadata dict a bit

            # TODO: set _PARTITION_DATE when inserting
            bq_rows = upload_rbe_results._convert_resultstore_actions_to_bigquery_rows(resultstore_actions, extra_metadata_columns)
            print('Collected %d rows to insert to bq' % len(bq_rows))

            if args.action_dump_dir:
                action_file_name = '%s/%s.json' % (args.action_dump_dir, invocation_id)
                with open(action_file_name, 'w') as f:
                    json.dump(resultstore_actions, f, indent=4, sort_keys=True)
                print('Dumped actions for invocation to %s' % action_file_name)
                action_bq_file_name = '%s/%s_bq.json' % (args.action_dump_dir, invocation_id)
                with open(action_bq_file_name, 'w') as f:
                    json.dump(bq_rows, f, indent=4, sort_keys=True)
                print('Dumped bq rows for invocation to %s' % action_bq_file_name)

            for row in bq_rows:
                newline_delimited_f.write(json.dumps(row))
                newline_delimited_f.write('\n')
            newline_delimited_f.flush()

    print('Wrote newline delimited JSON file to upload to bq')

    # bq mk --schema=bazel_results_schema.json --time_partitioning_type DAY --time_partitioning_expiration 31536000000 jtattermusch_experiment.bazel_results_staging
    
    # bq load -schema=bazel_results_schema.json --source_format NEWLINE_DELIMITED_JSON jtattermusch_experiment.resultstore_bazel_metadata_20220830_2 actions_newline_delimited.json

        # TODO: upload to bq...
         # TODO: use newline delimited JSON..

         # TODO: convert to bigquery
        
        # optionally dump the action data for all invocations
        

    # step 3: upload records to bigquery table
    #if not args.skip_upload:
    #    # BigQuery sometimes fails with large uploads, so batch 1,000 rows at a time.
    #    MAX_ROWS = 1000
    #    for i in range(0, len(all_bq_rows), MAX_ROWS):
    #        _upload_results_to_bq(all_bq_rows[i:i + MAX_ROWS])
    #else:
    #    print('Skipped upload to bigquery.')


    # bq_rows = []
    # for selected_invocation in selected_invocations:
    #     row = {
    #             'insertId': str(uuid.uuid4()),
    #             'json': selected_invocation
    #     }
    #     bq_rows.append(row)
    # _upload_invocations_to_bq(bq_rows)

    # TODO: resulting file will be too big.
    #actions_for_invocations = []
    # for selected_invocation in selected_invocations:
    #     invocation_id = selected_invocation['invocation_id']
    #     resultstore_actions = _get_resultstore_actions_for_invocation(api_key, invocation_id)
    #     print('Collected %d actions for invocation %s' % (len(resultstore_actions), invocation_id))

    #      # TODO: use newline delimited JSON..

    #      # TODO: convert to bigquery
        
    #     # optionally dump the action data for all invocations
    #     if args.action_dump_dir:
    #         action_file_name = '%s/%s.json' % (args.action_dump_dir, invocation_id)
    #         with open(action_file_name, 'w') as f:
    #             json.dump(resultstore_actions, f, indent=4, sort_keys=True)
    #         print('Dumped actions for invocations to %s' % action_file_name)

        #actions_for_invocations.append(resultstore_actions)
    

    


# turn file into newline delimited JSON format
#cat invocation.json | jq -c '.[]' >invocations_newline_delimited.json
 
# upload the data into a bigquery table
#your default project should be "grpc-testing"
#bq load --autodetect --source_format NEWLINE_DELIMITED_JSON jtattermusch_experiment.resultstore_bazel_metadata_20220629 invocations_newline_delimited.json

# 1. run for past N days
# 2. filter out existing invocations
# 3. only insert finished invocations that aren't still in progress (status = "BUILDING")
# 4. select the invocations we care about (e.g. for master jobs)
# 5. collect test actions for to-be-inserted resultstore invocations
