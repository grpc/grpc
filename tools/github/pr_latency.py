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
"""Measure the time between PR creation and completion of all tests.

You'll need a github API token to avoid being rate-limited. See
https://help.github.com/articles/creating-a-personal-access-token-for-the-command-line/

This script goes over the most recent 100 pull requests. For PRs with a single
commit, it uses the PR's creation as the initial time; othewise, it uses the
date of the last commit. This is somewhat fragile, and imposed by the fact that
GitHub reports a PR's updated timestamp for any event that modifies the PR (e.g.
comments), not just the addition of new commits.

In addition, it ignores latencies greater than five hours, as that's likely due
to a manual re-run of tests.
"""

from __future__ import absolute_import
from __future__ import division
from __future__ import print_function

import json
import logging
import pprint
import urllib2

from datetime import datetime, timedelta

logging.basicConfig(format='%(asctime)s %(message)s')

PRS = 'https://api.github.com/repos/grpc/grpc/pulls?state=open&per_page=100'
COMMITS = 'https://api.github.com/repos/grpc/grpc/pulls/{pr_number}/commits'


def gh(url):
    request = urllib2.Request(url)
    if TOKEN:
        request.add_header('Authorization', 'token {}'.format(TOKEN))
    response = urllib2.urlopen(request)
    return response.read()


def print_csv_header():
    print('pr,base_time,test_time,latency_seconds,successes,failures,errors')


def output(pr,
           base_time,
           test_time,
           diff_time,
           successes,
           failures,
           errors,
           mode='human'):
    if mode == 'human':
        print(
            "PR #{} base time: {} UTC, Tests completed at: {} UTC. Latency: {}."
            "\n\tSuccesses: {}, Failures: {}, Errors: {}".format(
                pr, base_time, test_time, diff_time, successes, failures,
                errors))
    elif mode == 'csv':
        print(','.join([
            str(pr),
            str(base_time),
            str(test_time),
            str(int((test_time - base_time).total_seconds())),
            str(successes),
            str(failures),
            str(errors)
        ]))


def parse_timestamp(datetime_str):
    return datetime.strptime(datetime_str, '%Y-%m-%dT%H:%M:%SZ')


def to_posix_timestamp(dt):
    return str((dt - datetime(1970, 1, 1)).total_seconds())


def get_pr_data():
    latest_prs = json.loads(gh(PRS))
    res = [{
        'number': pr['number'],
        'created_at': parse_timestamp(pr['created_at']),
        'updated_at': parse_timestamp(pr['updated_at']),
        'statuses_url': pr['statuses_url']
    } for pr in latest_prs]
    return res


def get_commits_data(pr_number):
    commits = json.loads(gh(COMMITS.format(pr_number=pr_number)))
    return {
        'num_commits': len(commits),
        'most_recent_date':
        parse_timestamp(commits[-1]['commit']['author']['date'])
    }


def get_status_data(statuses_url, system):
    status_url = statuses_url.replace('statuses', 'status')
    statuses = json.loads(gh(status_url + '?per_page=100'))
    successes = 0
    failures = 0
    errors = 0
    latest_datetime = None
    if not statuses: return None
    if system == 'kokoro': string_in_target_url = 'kokoro'
    elif system == 'jenkins': string_in_target_url = 'grpc-testing'
    for status in statuses['statuses']:
        if not status['target_url'] or string_in_target_url not in status['target_url']:
            continue  # Ignore jenkins
        if status['state'] == 'pending': return None
        elif status['state'] == 'success': successes += 1
        elif status['state'] == 'failure': failures += 1
        elif status['state'] == 'error': errors += 1
        if not latest_datetime:
            latest_datetime = parse_timestamp(status['updated_at'])
        else:
            latest_datetime = max(latest_datetime,
                                  parse_timestamp(status['updated_at']))
    # First status is the most recent one.
    if any([successes, failures, errors
           ]) and sum([successes, failures, errors]) > 15:
        return {
            'latest_datetime': latest_datetime,
            'successes': successes,
            'failures': failures,
            'errors': errors
        }
    else:
        return None


def build_args_parser():
    import argparse
    parser = argparse.ArgumentParser()
    parser.add_argument(
        '--format',
        type=str,
        choices=['human', 'csv'],
        default='human',
        help='Output format: are you a human or a machine?')
    parser.add_argument(
        '--system',
        type=str,
        choices=['jenkins', 'kokoro'],
        required=True,
        help='Consider only the given CI system')
    parser.add_argument(
        '--token',
        type=str,
        default='',
        help='GitHub token to use its API with a higher rate limit')
    return parser


def main():
    import sys
    global TOKEN
    args_parser = build_args_parser()
    args = args_parser.parse_args()
    TOKEN = args.token
    if args.format == 'csv': print_csv_header()
    for pr_data in get_pr_data():
        commit_data = get_commits_data(pr_data['number'])
        # PR with a single commit -> use the PRs creation time.
        # else -> use the latest commit's date.
        base_timestamp = pr_data['updated_at']
        if commit_data['num_commits'] > 1:
            base_timestamp = commit_data['most_recent_date']
        else:
            base_timestamp = pr_data['created_at']
        last_status = get_status_data(pr_data['statuses_url'], args.system)
        if last_status:
            diff = last_status['latest_datetime'] - base_timestamp
            if diff < timedelta(hours=5):
                output(
                    pr_data['number'],
                    base_timestamp,
                    last_status['latest_datetime'],
                    diff,
                    last_status['successes'],
                    last_status['failures'],
                    last_status['errors'],
                    mode=args.format)


if __name__ == '__main__':
    main()
