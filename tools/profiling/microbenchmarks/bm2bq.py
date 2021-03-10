#!/usr/bin/env python3
#
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

# Convert google-benchmark json output to something that can be uploaded to
# BigQuery

import sys
import json
import csv
import bm_json
import json
import subprocess

columns = []

for row in json.loads(
        subprocess.check_output(
            ['bq', '--format=json', 'show',
             'microbenchmarks.microbenchmarks']))['schema']['fields']:
    columns.append((row['name'], row['type'].lower()))

SANITIZE = {
    'integer': int,
    'float': float,
    'boolean': bool,
    'string': str,
    'timestamp': str,
}

if sys.argv[1] == '--schema':
    print(',\n'.join('%s:%s' % (k, t.upper()) for k, t in columns))
    sys.exit(0)

with open(sys.argv[1]) as f:
    js = json.loads(f.read())

if len(sys.argv) > 2:
    with open(sys.argv[2]) as f:
        js2 = json.loads(f.read())
else:
    js2 = None

# TODO(jtattermusch): write directly to a file instead of stdout
writer = csv.DictWriter(sys.stdout, [c for c, t in columns])

for row in bm_json.expand_json(js, js2):
    sane_row = {}
    for name, sql_type in columns:
        if name in row:
            if row[name] == '':
                continue
            sane_row[name] = SANITIZE[sql_type](row[name])
    writer.writerow(sane_row)
