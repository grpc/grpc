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

import yaml
import argparse
import datetime
import csv

argp = argparse.ArgumentParser(description='Convert cloc yaml to bigquery csv')
argp.add_argument('-i', '--input', type=str)
argp.add_argument('-d',
                  '--date',
                  type=str,
                  default=datetime.date.today().strftime('%Y-%m-%d'))
argp.add_argument('-o', '--output', type=str, default='out.csv')
args = argp.parse_args()

data = yaml.load(open(args.input).read())
with open(args.output, 'w') as outf:
    writer = csv.DictWriter(
        outf, ['date', 'name', 'language', 'code', 'comment', 'blank'])
    for key, value in data.iteritems():
        if key == 'header':
            continue
        if key == 'SUM':
            continue
        if key.startswith('third_party/'):
            continue
        row = {'name': key, 'date': args.date}
        row.update(value)
        writer.writerow(row)
