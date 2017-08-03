# -*- coding: utf-8 -*-
from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import os
import sys
import logging
logging.basicConfig(format='%(asctime)s %(message)s')

gcp_utils_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '../gcp/utils'))
sys.path.append(gcp_utils_dir)

import big_query_utils


def get_flaky_tests(period, limit=None):
  """ period is one of "WEEK", "DAY", etc.
  (see https://cloud.google.com/bigquery/docs/reference/standard-sql/functions-and-operators#date_add). """

  bq = big_query_utils.create_big_query()
  query = """
SELECT
  filtered_test_name,
  FIRST(timestamp),
  FIRST(build_url),
FROM (
  SELECT
    REGEXP_REPLACE(test_name, r'/\d+', '') AS filtered_test_name,
    result,
    build_url,
    timestamp
  FROM
    [grpc-testing:jenkins_test_results.aggregate_results]
  WHERE
    timestamp >= DATE_ADD(CURRENT_DATE(), -1, "{period}")
    AND NOT REGEXP_MATCH(job_name, '.*portability.*'))
GROUP BY
  filtered_test_name,
  timestamp,
  build_url
HAVING
  SUM(result != 'PASSED'
    AND result != 'SKIPPED') > 0
ORDER BY
  timestamp DESC
""".format(period=period)
  if limit:
    query += '\n LIMIT {}'.format(limit)
  query_job = big_query_utils.sync_query_job(bq, 'grpc-testing', query)
  page = bq.jobs().getQueryResults(
      pageToken=None, **query_job['jobReference']).execute(num_retries=3)
  testname_to_ts_url_pair = {row['f'][0]['v']: (row['f'][1]['v'], row['f'][2]['v']) for row in page['rows']}
  return testname_to_ts_url_pair


def get_new_flakes():
  weekly = get_flaky_tests("WEEK")
  last_24 = get_flaky_tests("DAY")
  weekly_names = set(weekly.keys())
  last_24_names = set(last_24.keys())
  logging.debug('|weekly_names| =', len(weekly_names))
  logging.debug('|last_24_names| =', len(last_24_names))
  new_flakes = last_24_names - weekly_names
  logging.debug('|new_flakes| = ', len(new_flakes))
  return {k: last_24[k] for k in new_flakes}


def main():
  import datetime
  new_flakes = get_new_flakes()
  if new_flakes:
    print("New flakes found:")
    for k, v in new_flakes.items():
      ts = int(float(v[0]))
      url = v[1]
      human_ts = datetime.datetime.utcfromtimestamp(ts).strftime('%Y-%m-%d %H:%M:%S UTC')
      print("Test: {}, Timestamp: {}, URL: {}".format(k, human_ts, url))


if __name__ == '__main__':
  main()
