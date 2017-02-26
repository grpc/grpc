#!/usr/bin/env python2.7
#
# Convert google-benchmark json output to something that can be uploaded to
# BigQuery
#
#
# Copyright 2017, gRPC authors
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#   http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

import sys
import json
import csv
import os

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

if sys.argv[1] == '--schema':
  print ',\n'.join('%s:%s' % (k, t.upper()) for k, t in columns)
  sys.exit(0)

with open(sys.argv[1]) as f:
  js = json.loads(f.read())

writer = csv.DictWriter(sys.stdout, [c for c,t in columns])

bm_specs = {
  'BM_UnaryPingPong': {
    'tpl': ['fixture', 'client_mutator', 'server_mutator'],
    'dyn': ['request_size', 'response_size'],
  },
  'BM_PumpStreamClientToServer': {
    'tpl': ['fixture'],
    'dyn': ['request_size'],
  },
  'BM_PumpStreamServerToClient': {
    'tpl': ['fixture'],
    'dyn': ['request_size'],
  },
  'BM_StreamingPingPong': {
    'tpl': ['fixture', 'client_mutator', 'server_mutator'],
    'dyn': ['request_size', 'request_count'],
  },
  'BM_StreamingPingPongMsgs': {
    'tpl': ['fixture', 'client_mutator', 'server_mutator'],
    'dyn': ['request_size'],
  },
  'BM_PumpStreamServerToClient_Trickle': {
    'tpl': [],
    'dyn': ['request_size', 'bandwidth_kilobits'],
  },
  'BM_ErrorStringOnNewError': {
    'tpl': ['fixture'],
    'dyn': [],
  },
  'BM_ErrorStringRepeatedly': {
    'tpl': ['fixture'],
    'dyn': [],
  },
  'BM_ErrorGetStatus': {
    'tpl': ['fixture'],
    'dyn': [],
  },
  'BM_ErrorGetStatusCode': {
    'tpl': ['fixture'],
    'dyn': [],
  },
  'BM_ErrorHttpError': {
    'tpl': ['fixture'],
    'dyn': [],
  },
  'BM_HasClearGrpcStatus': {
    'tpl': ['fixture'],
    'dyn': [],
  },
  'BM_IsolatedFilter' : {
    'tpl': ['fixture', 'client_mutator'],
    'dyn': [],
  },
  'BM_HpackEncoderEncodeHeader' : {
    'tpl': ['fixture'],
    'dyn': ['end_of_stream', 'request_size'],
  },
  'BM_HpackParserParseHeader' : {
    'tpl': ['fixture'],
    'dyn': [],
  },
}

def numericalize(s):
  if not s: return ''
  if s[-1] == 'k':
    return int(s[:-1]) * 1024
  if s[-1] == 'M':
    return int(s[:-1]) * 1024 * 1024
  if 0 <= (ord(s[-1]) - ord('0')) <= 9:
    return int(s)
  assert 'not a number: %s' % s

def parse_name(name):
  if '<' not in name and '/' not in name and name not in bm_specs:
    return {'name': name}
  rest = name
  out = {}
  tpl_args = []
  dyn_args = []
  if '<' in rest:
    tpl_bit = rest[rest.find('<') + 1 : rest.rfind('>')]
    arg = ''
    nesting = 0
    for c in tpl_bit:
      if c == '<':
        nesting += 1
        arg += c
      elif c == '>':
        nesting -= 1
        arg += c
      elif c == ',':
        if nesting == 0:
          tpl_args.append(arg.strip())
          arg = ''
        else:
          arg += c
      else:
        arg += c
    tpl_args.append(arg.strip())
    rest = rest[:rest.find('<')] + rest[rest.rfind('>') + 1:]
  if '/' in rest:
    s = rest.split('/')
    rest = s[0]
    dyn_args = s[1:]
  name = rest
  assert name in bm_specs, 'bm_specs needs to be expanded for %s' % name
  assert len(dyn_args) == len(bm_specs[name]['dyn'])
  assert len(tpl_args) == len(bm_specs[name]['tpl'])
  out['name'] = name
  out.update(dict((k, numericalize(v)) for k, v in zip(bm_specs[name]['dyn'], dyn_args)))
  out.update(dict(zip(bm_specs[name]['tpl'], tpl_args)))
  return out

for bm in js['benchmarks']:
  context = js['context']
  if 'label' in bm:
    labels_list = [s.split(':') for s in bm['label'].strip().split(' ') if len(s) and s[0] != '#']
    for el in labels_list:
      el[0] = el[0].replace('/iter', '_per_iteration')
    labels = dict(labels_list)
  else:
    labels = {}
  row = {
    'jenkins_build': os.environ.get('BUILD_NUMBER', ''),
    'jenkins_job': os.environ.get('JOB_NAME', ''),
  }
  row.update(context)
  row.update(bm)
  row.update(parse_name(row['name']))
  row.update(labels)
  if 'label' in row:
    del row['label']
  writer.writerow(row)
