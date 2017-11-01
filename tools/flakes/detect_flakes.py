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

"""Detect new flakes and create issues for them"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import datetime
import json
import logging
import os
import pprint
import sys
import urllib2
from collections import namedtuple

gcp_utils_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '../gcp/utils'))
sys.path.append(gcp_utils_dir)

import big_query_utils

GH_ISSUES_URL = 'https://api.github.com/repos/grpc/grpc/issues'
KOKORO_BASE_URL = 'https://kokoro2.corp.google.com/job/'

def gh(url, data=None):
  request = urllib2.Request(url, data=data)
  assert TOKEN
  request.add_header('Authorization', 'token {}'.format(TOKEN))
  if data:
    request.add_header('Content-type', 'application/json')
  response = urllib2.urlopen(request)
  if 200 <= response.getcode() < 300:
    return json.loads(response.read())
  else:
    raise ValueError('Error ({}) accessing {}'.format(
        response.getcode(), response.geturl()))


def create_gh_issue(title, body, labels):
  data = json.dumps({'title': title,
                     'body': body,
                     'labels': labels})
  response = gh(GH_ISSUES_URL, data)
  issue_url = response['html_url']
  print('Issue {} created for {}'.format(issue_url, title))


def build_kokoro_url(job_name, build_id):
  job_path = '{}/{}'.format('/job/'.join(job_name.split('/')), build_id)
  return KOKORO_BASE_URL + job_path


def create_issues(new_flakes):
  for test_name, results_row in new_flakes.items():
    poll_strategy, job_name, build_id, timestamp = results_row
    url = build_kokoro_url(job_name, build_id)
    title = 'New Flake: ' + test_name
    body = '- Test: {}\n- Poll Strategy: {}\n- URL: {}'.format(
        test_name, poll_strategy, url)
    labels = ['infra/New Flakes']
    create_gh_issue(title, body, labels)


def print_table(table, format):
  for test_name, results_row in table.items():
    poll_strategy, job_name, build_id, timestamp = results_row
    ts = int(float(timestamp))
    # TODO(dgq): timezone handling is wrong. We need to determine the timezone
    # of the computer running this script.
    human_ts = datetime.datetime.utcfromtimestamp(ts).strftime('%Y-%m-%d %H:%M:%S UTC')
    full_kokoro_url = build_kokoro_url(job_name, build_id)
    if format == 'human':
      print("\t- Test: {}, Polling: {}, Timestamp: {}, url: {}".format(
          test_name, poll_strategy, human_ts, full_kokoro_url))
    else:
      assert(format == 'csv')
      print("{},{},{}".format(test_name, ts, human_ts, full_kokoro_url))

Row = namedtuple('Row', ['poll_strategy', 'job_name', 'build_id', 'timestamp'])
def get_flaky_tests(from_date, to_date, limit=None):
  """Return flaky tests for date range (from_date, to_date], where both are
  strings of the form "YYYY-MM-DD" """
  bq = big_query_utils.create_big_query()
  query = """
#standardSQL
SELECT
  RTRIM(LTRIM(REGEXP_REPLACE(filtered_test_name, r'(/\d+)|(bins/.+/)|(cmake/.+/.+/)', ''))) AS test_binary,
  REGEXP_EXTRACT(test_name, r'GRPC_POLL_STRATEGY=(\w+)') AS poll_strategy,
  job_name,
  build_id,
  timestamp
FROM (
  SELECT
    REGEXP_REPLACE(test_name, r'(/\d+)|(GRPC_POLL_STRATEGY=.+)', '') AS filtered_test_name,
    test_name,
    job_name,
    build_id,
    timestamp
  FROM `grpc-testing.jenkins_test_results.aggregate_results`
  WHERE
  timestamp > TIMESTAMP("{from_date}")
  AND timestamp <= TIMESTAMP("{to_date}")
    AND NOT REGEXP_CONTAINS(job_name, 'portability')
    AND result != 'PASSED' AND result != 'SKIPPED'
)
ORDER BY timestamp desc""".format(
    from_date=from_date.isoformat(), to_date=to_date.isoformat())
  if limit:
    query += '\n LIMIT {}'.format(limit)
  logging.debug("Query:\n%s", query)
  query_job = big_query_utils.sync_query_job(bq, 'grpc-testing', query)
  page = bq.jobs().getQueryResults(
      pageToken=None, **query_job['jobReference']).execute(num_retries=3)
  rows = page.get('rows')
  if rows:
    return {row['f'][0]['v']:
            Row(poll_strategy=row['f'][1]['v'],
                job_name=row['f'][2]['v'],
                build_id=row['f'][3]['v'],
                timestamp=row['f'][4]['v'])
            for row in rows}
  else:
    return {}


def parse_isodate(date_str):
  return datetime.datetime.strptime(date_str, "%Y-%m-%d").date()


def get_new_flakes(args):
  """The from_date_str argument marks the beginning of the "calibration", used
  to establish the set of pre-existing flakes, which extends over
  "calibration_days".  After the calibration period, "reporting_days" is the
  length of time during which new flakes will be reported.

from
date
  |--------------------|---------------|
  ^____________________^_______________^
       calibration         reporting
         days                days
  """
  dates = process_date_args(args)
  calibration_results = get_flaky_tests(dates['calibration']['begin'],
                                        dates['calibration']['end'])
  reporting_results = get_flaky_tests(dates['reporting']['begin'],
                                      dates['reporting']['end'])
  logging.debug('Calibration results: %s', pprint.pformat(calibration_results))
  logging.debug('Reporting results: %s', pprint.pformat(reporting_results))

  calibration_names = set(calibration_results.keys())
  logging.info('|calibration_results (%s, %s]| = %d',
               dates['calibration']['begin'].isoformat(),
               dates['calibration']['end'].isoformat(),
               len(calibration_names))
  reporting_names = set(reporting_results.keys())
  logging.info('|reporting_results (%s, %s]| = %d',
               dates['reporting']['begin'].isoformat(),
               dates['reporting']['end'].isoformat(),
               len(reporting_names))

  new_flakes = reporting_names - calibration_names
  logging.info('|new_flakes| = %d', len(new_flakes))
  return {k: reporting_results[k] for k in new_flakes}


def build_args_parser():
  import argparse, datetime
  parser = argparse.ArgumentParser()
  today = datetime.date.today()
  a_week_ago = today - datetime.timedelta(days=7)
  parser.add_argument('--calibration_days', type=int, default=7,
                      help='How many days to consider for pre-existing flakes.')
  parser.add_argument('--reporting_days', type=int, default=1,
                      help='How many days to consider for the detection of new flakes.')
  parser.add_argument('--count_only', dest='count_only', action='store_true',
                      help='Display only number of new flakes.')
  parser.set_defaults(count_only=False)
  parser.add_argument('--create_issues', dest='create_issues', action='store_true',
                      help='Create issues for all new flakes.')
  parser.set_defaults(create_issues=False)
  parser.add_argument('--token', type=str, default='',
                      help='GitHub token to use its API with a higher rate limit')
  parser.add_argument('--format', type=str, choices=['human', 'csv'],
                      default='human', help='Output format: are you a human or a machine?')
  parser.add_argument('--loglevel', type=str,
                      choices=['INFO', 'DEBUG', 'WARNING', 'ERROR', 'CRITICAL'],
                      default='WARNING', help='Logging level.')
  return parser


def process_date_args(args):
  calibration_begin = (datetime.date.today() -
                       datetime.timedelta(days=args.calibration_days) -
                       datetime.timedelta(days=args.reporting_days))
  calibration_end = calibration_begin + datetime.timedelta(days=args.calibration_days)
  reporting_begin = calibration_end
  reporting_end = reporting_begin + datetime.timedelta(days=args.reporting_days)
  return {'calibration': {'begin': calibration_begin, 'end': calibration_end},
          'reporting': {'begin': reporting_begin, 'end': reporting_end }}


def main():
  global TOKEN
  args_parser = build_args_parser()
  args = args_parser.parse_args()
  if args.create_issues and not args.token:
    raise ValueError('Missing --token argument, needed to create GitHub issues')
  TOKEN = args.token

  logging_level = getattr(logging, args.loglevel)
  logging.basicConfig(format='%(asctime)s %(message)s', level=logging_level)
  new_flakes = get_new_flakes(args)

  dates = process_date_args(args)

  dates_info_string = 'from {} until {} (calibrated from {} until {})'.format(
      dates['reporting']['begin'].isoformat(),
      dates['reporting']['end'].isoformat(),
      dates['calibration']['begin'].isoformat(),
      dates['calibration']['end'].isoformat())

  if args.format == 'human':
    if args.count_only:
      print(len(new_flakes), dates_info_string)
    elif new_flakes:
      found_msg = 'Found {} new flakes {}'.format(len(new_flakes), dates_info_string)
      print(found_msg)
      print('*' * len(found_msg))
      print_table(new_flakes, 'human')
      create_issues(new_flakes)
    else:
      print('No new flakes found '.format(len(new_flakes)), dates_info_string)
  elif args.format == 'csv':
    if args.count_only:
      print('from_date,to_date,count')
      print('{},{},{}'.format(
          dates['reporting']['begin'].isoformat(),
          dates['reporting']['end'].isoformat(),
          len(new_flakes)))
    else:
      print('test,timestamp,readable_timestamp,url')
      print_table(new_flakes, 'csv')
  else:
    raise ValueError('Invalid argument for --format: {}'.format(args.format))


if __name__ == '__main__':
  main()
