#!/usr/bin/env python
# Copyright 2017, Google Inc.
# All rights reserved.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#     * Redistributions of source code must retain the above copyright
# notice, this list of conditions and the following disclaimer.
#     * Redistributions in binary form must reproduce the above
# copyright notice, this list of conditions and the following disclaimer
# in the documentation and/or other materials provided with the
# distribution.
#     * Neither the name of Google Inc. nor the names of its
# contributors may be used to endorse or promote products derived from
# this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.


import yaml
import argparse
import datetime
import csv

argp = argparse.ArgumentParser(description='Convert cloc yaml to bigquery csv')
argp.add_argument('-i', '--input', type=str)
argp.add_argument('-d', '--date', type=str, default=datetime.date.today().strftime('%Y-%m-%d'))
argp.add_argument('-o', '--output', type=str, default='out.csv')
args = argp.parse_args()

data = yaml.load(open(args.input).read())
with open(args.output, 'w') as outf:
  writer = csv.DictWriter(outf, ['date', 'name', 'language', 'code', 'comment', 'blank'])
  for key, value in data.iteritems():
    if key == 'header': continue
    if key == 'SUM': continue
    if key.startswith('third_party/'): continue
    row = {'name': key, 'date': args.date}
    row.update(value)
    writer.writerow(row)

