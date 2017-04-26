#!/usr/bin/env python2.7
#
# Convert google-benchmark json output to something that can be uploaded to
# BigQuery
#
#
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

import sys
import json
import csv
import bm_json

columns = [
  ('jenkins_build', 'integer'),
  ('jenkins_job', 'string'),
  ('date', 'timestamp'),
  ('cpu_scaling_enabled', 'boolean'),
  ('num_cpus', 'integer'),
  ('mhz_per_cpu', 'integer'),
  ('library_build_type', 'string'),
  ('name', 'string'),
  ('fixture', 'string'),
  ('client_mutator', 'string'),
  ('server_mutator', 'string'),
  ('request_size', 'integer'),
  ('response_size', 'integer'),
  ('request_count', 'integer'),
  ('iterations', 'integer'),
  ('time_unit', 'string'),
  ('real_time', 'integer'),
  ('cpu_time', 'integer'),
  ('bytes_per_second', 'float'),
  ('allocs_per_iteration', 'float'),
  ('locks_per_iteration', 'float'),
  ('writes_per_iteration', 'float'),
  ('bandwidth_kilobits', 'integer'),
  ('cli_transport_stalls_per_iteration', 'float'),
  ('cli_stream_stalls_per_iteration', 'float'),
  ('svr_transport_stalls_per_iteration', 'float'),
  ('svr_stream_stalls_per_iteration', 'float'),
  ('atm_cas_per_iteration', 'float'),
  ('atm_add_per_iteration', 'float'),
  ('end_of_stream', 'boolean'),
  ('header_bytes_per_iteration', 'float'),
  ('framing_bytes_per_iteration', 'float'),
]

SANITIZE = {
  'integer': int,
  'float': float,
  'boolean': bool,
  'string': str,
  'timestamp': str,
}

if sys.argv[1] == '--schema':
  print ',\n'.join('%s:%s' % (k, t.upper()) for k, t in columns)
  sys.exit(0)

with open(sys.argv[1]) as f:
  js = json.loads(f.read())

if len(sys.argv) > 2:
  with open(sys.argv[2]) as f:
    js2 = json.loads(f.read())
else:
  js2 = None

writer = csv.DictWriter(sys.stdout, [c for c,t in columns])

for row in bm_json.expand_json(js, js2):
  sane_row = {}
  for name, sql_type in columns:
    if name in row:
      if row[name] == '': continue
      sane_row[name] = SANITIZE[sql_type](row[name])
  writer.writerow(sane_row)

