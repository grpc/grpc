# Copyright 2019 gRPC authors.
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

from github import Github, Label
from datetime import datetime, timedelta
from time import time
from google.cloud import bigquery

ACCESS_TOKEN = ""


def get_stats_from_github():
    # Please set the access token properly before deploying.
    assert ACCESS_TOKEN
    g = Github(ACCESS_TOKEN)
    print g.rate_limiting
    repo = g.get_repo('grpc/grpc')

    LABEL_LANG = set(label for label in repo.get_labels()
                     if label.name.split('/')[0] == 'lang')
    LABEL_KIND_BUG = repo.get_label('kind/bug')
    LABEL_PRIORITY_P0 = repo.get_label('priority/P0')
    LABEL_PRIORITY_P1 = repo.get_label('priority/P1')
    LABEL_PRIORITY_P2 = repo.get_label('priority/P2')

    def is_untriaged(issue):
        key_labels = set()
        for label in issue.labels:
            label_kind = label.name.split('/')[0]
            if label_kind in ('lang', 'kind', 'priority'):
                key_labels.add(label_kind)
        return len(key_labels) < 3

    untriaged_open_issues = [
        issue for issue in repo.get_issues(state='open')
        if issue.pull_request is None and is_untriaged(issue)
    ]
    total_bugs = [
        issue for issue in repo.get_issues(state='all', labels=[LABEL_KIND_BUG])
        if issue.pull_request is None
    ]

    lang_to_stats = {}
    for lang in LABEL_LANG:
        lang_bugs = filter(lambda bug: lang in bug.labels, total_bugs)
        closed_bugs = filter(lambda bug: bug.state == 'closed', lang_bugs)
        open_bugs = filter(lambda bug: bug.state == 'open', lang_bugs)
        open_p0_bugs = filter(lambda bug: LABEL_PRIORITY_P0 in bug.labels,
                              open_bugs)
        open_p1_bugs = filter(lambda bug: LABEL_PRIORITY_P1 in bug.labels,
                              open_bugs)
        open_p2_bugs = filter(lambda bug: LABEL_PRIORITY_P2 in bug.labels,
                              open_bugs)
        lang_to_stats[lang] = [
            len(lang_bugs),
            len(closed_bugs),
            len(open_bugs),
            len(open_p0_bugs),
            len(open_p1_bugs),
            len(open_p2_bugs)
        ]
    return len(untriaged_open_issues), lang_to_stats


def insert_stats_to_db(untriaged_open_issues, lang_to_stats):
    timestamp = time()
    client = bigquery.Client()
    dataset_ref = client.dataset('github_issues')
    table_ref = dataset_ref.table('untriaged_issues')
    table = client.get_table(table_ref)
    errors = client.insert_rows(table, [(timestamp, untriaged_open_issues)])
    table_ref = dataset_ref.table('bug_stats')
    table = client.get_table(table_ref)
    rows = []
    for lang, stats in lang_to_stats.iteritems():
        rows.append((timestamp, lang.name[5:]) + tuple(stats))
    errors = client.insert_rows(table, rows)


def fetch():
    untriaged_open_issues, lang_to_stats = get_stats_from_github()
    insert_stats_to_db(untriaged_open_issues, lang_to_stats)
