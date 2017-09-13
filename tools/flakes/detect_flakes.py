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

"""Detect new flakes introduced in the last 24h hours with respect to the
previous six days"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import datetime
import os
import sys
import logging
logging.basicConfig(format='%(asctime)s %(message)s')

gcp_utils_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '../gcp/utils'))
sys.path.append(gcp_utils_dir)

import big_query_utils

def print_table(table):
    kokoro_base_url = 'https://kokoro.corp.google.com/job/'
    for k, v in table.items():
      job_name = v[0]
      build_id = v[1]
      ts = int(float(v[2]))
      # TODO(dgq): timezone handling is wrong. We need to determine the timezone
      # of the computer running this script.
      human_ts = datetime.datetime.utcfromtimestamp(ts).strftime('%Y-%m-%d %H:%M:%S PDT')
      job_path = '{}/{}'.format('/job/'.join(job_name.split('/')), build_id)
      full_kokoro_url = kokoro_base_url + job_path
      print("Test: {}, Timestamp: {}, url: {}\n".format(k, human_ts, full_kokoro_url))


def get_flaky_tests(days_lower_bound, days_upper_bound, limit=None):
  """ period is one of "WEEK", "DAY", etc.
  (see https://cloud.google.com/bigquery/docs/reference/standard-sql/functions-and-operators#date_add). """

  bq = big_query_utils.create_big_query()
  query = """
SELECT
  REGEXP_REPLACE(test_name, r'/\d+', '') AS filtered_test_name,
  job_name,
  build_id,
  timestamp
FROM
  [grpc-testing:jenkins_test_results.aggregate_results]
WHERE
    timestamp > DATE_ADD(CURRENT_DATE(), {days_lower_bound}, "DAY")
    AND timestamp <= DATE_ADD(CURRENT_DATE(), {days_upper_bound}, "DAY")
  AND NOT REGEXP_MATCH(job_name, '.*portability.*')
  AND result != 'PASSED' AND result != 'SKIPPED'
ORDER BY timestamp desc
""".format(days_lower_bound=days_lower_bound, days_upper_bound=days_upper_bound)
  if limit:
    query += '\n LIMIT {}'.format(limit)
  query_job = big_query_utils.sync_query_job(bq, 'grpc-testing', query)
  page = bq.jobs().getQueryResults(
      pageToken=None, **query_job['jobReference']).execute(num_retries=3)
  rows = page.get('rows')
  if rows:
    return {row['f'][0]['v']:
            (row['f'][1]['v'], row['f'][2]['v'], row['f'][3]['v'])
            for row in rows}
  else:
    return {}


def get_new_flakes():
  last_week_sans_yesterday = get_flaky_tests(-14, -1)
  last_24 = get_flaky_tests(0, +1)
  last_week_sans_yesterday_names = set(last_week_sans_yesterday.keys())
  last_24_names = set(last_24.keys())
  logging.debug('|last_week_sans_yesterday| =', len(last_week_sans_yesterday_names))
  logging.debug('|last_24_names| =', len(last_24_names))
  new_flakes = last_24_names - last_week_sans_yesterday_names
  logging.debug('|new_flakes| = ', len(new_flakes))
  return {k: last_24[k] for k in new_flakes}


def main():
  new_flakes = get_new_flakes()
  if new_flakes:
    print("Found {} new flakes:".format(len(new_flakes)))
    print_table(new_flakes)
  else:
    print("No new flakes found!")


if __name__ == '__main__':
  main()
