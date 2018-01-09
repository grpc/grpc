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
import urllib
import urllib2
from collections import namedtuple

gcp_utils_dir = os.path.abspath(
    os.path.join(os.path.dirname(__file__), '../gcp/utils'))
sys.path.append(gcp_utils_dir)

import big_query_utils

GH_ISSUE_CREATION_URL = 'https://api.github.com/repos/grpc/grpc/issues'
GH_ISSUE_SEARCH_URL = 'https://api.github.com/search/issues'
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


def search_gh_issues(search_term, status='open'):
    params = ' '.join((search_term, 'is:issue', 'is:open', 'repo:grpc/grpc'))
    qargs = urllib.urlencode({'q': params})
    url = '?'.join((GH_ISSUE_SEARCH_URL, qargs))
    response = gh(url)
    return response


def create_gh_issue(title, body, labels, assignees=[]):
    params = {'title': title, 'body': body, 'labels': labels}
    if assignees:
        params['assignees'] = assignees
    data = json.dumps(params)
    response = gh(GH_ISSUE_CREATION_URL, data)
    issue_url = response['html_url']
    print('Created issue {} for {}'.format(issue_url, title))


def build_kokoro_url(job_name, build_id):
    job_path = '{}/{}'.format('/job/'.join(job_name.split('/')), build_id)
    return KOKORO_BASE_URL + job_path


def create_issues(new_flakes, always_create):
    for test_name, results_row in new_flakes.items():
        poll_strategy, job_name, build_id, timestamp = results_row
        # TODO(dgq): the Kokoro URL has a limited lifetime. The permanent and ideal
        # URL would be the sponge one, but there's currently no easy way to retrieve
        # it.
        url = build_kokoro_url(job_name, build_id)
        title = 'New Failure: ' + test_name
        body = '- Test: {}\n- Poll Strategy: {}\n- URL: {}'.format(
            test_name, poll_strategy, url)
        labels = ['infra/New Failure']
        if always_create:
            proceed = True
        else:
            preexisting_issues = search_gh_issues(test_name)
            if preexisting_issues['total_count'] > 0:
                print('\nFound {} issues for "{}":'.format(
                    preexisting_issues['total_count'], test_name))
                for issue in preexisting_issues['items']:
                    print('\t"{}" ; URL: {}'.format(issue['title'],
                                                    issue['html_url']))
            else:
                print(
                    '\nNo preexisting issues found for "{}"'.format(test_name))
            proceed = raw_input(
                'Create issue for:\nTitle: {}\nBody: {}\n[Y/n] '.format(
                    title, body)) in ('y', 'Y', '')
        if proceed:
            assignees_str = raw_input(
                'Asignees? (comma-separated, leave blank for unassigned): ')
            assignees = [
                assignee.strip() for assignee in assignees_str.split(',')
            ]
            create_gh_issue(title, body, labels, assignees)


def print_table(table, format):
    first_time = True
    for test_name, results_row in table.items():
        poll_strategy, job_name, build_id, timestamp = results_row
        full_kokoro_url = build_kokoro_url(job_name, build_id)
        if format == 'human':
            print("\t- Test: {}, Polling: {}, Timestamp: {}, url: {}".format(
                test_name, poll_strategy, timestamp, full_kokoro_url))
        else:
            assert (format == 'csv')
            if first_time:
                print('test,timestamp,url')
                first_time = False
            print("{},{},{}".format(test_name, timestamp, full_kokoro_url))


Row = namedtuple('Row', ['poll_strategy', 'job_name', 'build_id', 'timestamp'])


def get_new_failures(dates):
    bq = big_query_utils.create_big_query()
    this_script_path = os.path.join(os.path.dirname(__file__))
    sql_script = os.path.join(this_script_path, 'sql/new_failures_24h.sql')
    with open(sql_script) as query_file:
        query = query_file.read().format(
            calibration_begin=dates['calibration']['begin'],
            calibration_end=dates['calibration']['end'],
            reporting_begin=dates['reporting']['begin'],
            reporting_end=dates['reporting']['end'])
    logging.debug("Query:\n%s", query)
    query_job = big_query_utils.sync_query_job(bq, 'grpc-testing', query)
    page = bq.jobs().getQueryResults(
        pageToken=None, **query_job['jobReference']).execute(num_retries=3)
    rows = page.get('rows')
    if rows:
        return {
            row['f'][0]['v']: Row(
                poll_strategy=row['f'][1]['v'],
                job_name=row['f'][2]['v'],
                build_id=row['f'][3]['v'],
                timestamp=row['f'][4]['v'])
            for row in rows
        }
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
    new_failures = get_new_failures(dates)
    logging.info('|new failures| = %d', len(new_failures))
    return new_failures


def build_args_parser():
    import argparse, datetime
    parser = argparse.ArgumentParser()
    today = datetime.date.today()
    a_week_ago = today - datetime.timedelta(days=7)
    parser.add_argument(
        '--calibration_days',
        type=int,
        default=7,
        help='How many days to consider for pre-existing flakes.')
    parser.add_argument(
        '--reporting_days',
        type=int,
        default=1,
        help='How many days to consider for the detection of new flakes.')
    parser.add_argument(
        '--count_only',
        dest='count_only',
        action='store_true',
        help='Display only number of new flakes.')
    parser.set_defaults(count_only=False)
    parser.add_argument(
        '--create_issues',
        dest='create_issues',
        action='store_true',
        help='Create issues for all new flakes.')
    parser.set_defaults(create_issues=False)
    parser.add_argument(
        '--always_create_issues',
        dest='always_create_issues',
        action='store_true',
        help='Always create issues for all new flakes. Otherwise,'
        ' interactively prompt for every issue.')
    parser.set_defaults(always_create_issues=False)
    parser.add_argument(
        '--token',
        type=str,
        default='',
        help='GitHub token to use its API with a higher rate limit')
    parser.add_argument(
        '--format',
        type=str,
        choices=['human', 'csv'],
        default='human',
        help='Output format: are you a human or a machine?')
    parser.add_argument(
        '--loglevel',
        type=str,
        choices=['INFO', 'DEBUG', 'WARNING', 'ERROR', 'CRITICAL'],
        default='WARNING',
        help='Logging level.')
    return parser


def process_date_args(args):
    calibration_begin = (
        datetime.date.today() - datetime.timedelta(days=args.calibration_days) -
        datetime.timedelta(days=args.reporting_days))
    calibration_end = calibration_begin + datetime.timedelta(
        days=args.calibration_days)
    reporting_begin = calibration_end
    reporting_end = reporting_begin + datetime.timedelta(
        days=args.reporting_days)
    return {
        'calibration': {
            'begin': calibration_begin,
            'end': calibration_end
        },
        'reporting': {
            'begin': reporting_begin,
            'end': reporting_end
        }
    }


def main():
    global TOKEN
    args_parser = build_args_parser()
    args = args_parser.parse_args()
    if args.create_issues and not args.token:
        raise ValueError(
            'Missing --token argument, needed to create GitHub issues')
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
            found_msg = 'Found {} new flakes {}'.format(
                len(new_flakes), dates_info_string)
            print(found_msg)
            print('*' * len(found_msg))
            print_table(new_flakes, 'human')
            if args.create_issues:
                create_issues(new_flakes, args.always_create_issues)
        else:
            print('No new flakes found '.format(len(new_flakes)),
                  dates_info_string)
    elif args.format == 'csv':
        if args.count_only:
            print('from_date,to_date,count')
            print('{},{},{}'.format(dates['reporting']['begin'].isoformat(),
                                    dates['reporting']['end'].isoformat(),
                                    len(new_flakes)))
        else:
            print_table(new_flakes, 'csv')
    else:
        raise ValueError('Invalid argument for --format: {}'.format(
            args.format))


if __name__ == '__main__':
    main()
