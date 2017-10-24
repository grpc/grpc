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

import os

_BM_SPECS = {
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
  'BM_PumpUnbalancedUnary_Trickle': {
    'tpl': [],
    'dyn': ['cli_req_size', 'svr_req_size', 'bandwidth_kilobits'],
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
  'BM_IsolatedFilter': {
    'tpl': ['fixture', 'client_mutator'],
    'dyn': [],
  },
  'BM_HpackEncoderEncodeHeader': {
    'tpl': ['fixture'],
    'dyn': ['end_of_stream', 'request_size'],
  },
  'BM_HpackParserParseHeader': {
    'tpl': ['fixture', 'on_header'],
    'dyn': [],
  },
  'BM_CallCreateDestroy': {
    'tpl': ['fixture'],
    'dyn': [],
  },
  'BM_Zalloc': {
    'tpl': [],
    'dyn': ['request_size'],
  },
  'BM_PollEmptyPollset_SpeedOfLight': {
    'tpl': [],
    'dyn': ['request_size', 'request_count'],
  },
  'BM_StreamCreateSendInitialMetadataDestroy': {
    'tpl': ['fixture'],
    'dyn': [],
  },
  'BM_TransportStreamSend': {
    'tpl': [],
    'dyn': ['request_size'],
  },
  'BM_TransportStreamRecv': {
    'tpl': [],
    'dyn': ['request_size'],
  },
  'BM_StreamingPingPongWithCoalescingApi': {
    'tpl': ['fixture', 'client_mutator', 'server_mutator'],
    'dyn': ['request_size', 'request_count', 'end_of_stream'],
  },
  'BM_Base16SomeStuff': {
    'tpl': [],
    'dyn': ['request_size'],
  }
}

def numericalize(s):
  if not s: return ''
  if s[-1] == 'k':
    return float(s[:-1]) * 1024
  if s[-1] == 'M':
    return float(s[:-1]) * 1024 * 1024
  if 0 <= (ord(s[-1]) - ord('0')) <= 9:
    return float(s)
  assert 'not a number: %s' % s

def parse_name(name):
  cpp_name = name
  if '<' not in name and '/' not in name and name not in _BM_SPECS:
    return {'name': name, 'cpp_name': name}
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
  print (name)
  print (dyn_args, _BM_SPECS[name]['dyn'])
  print (tpl_args, _BM_SPECS[name]['tpl'])
  assert name in _BM_SPECS, '_BM_SPECS needs to be expanded for %s' % name
  assert len(dyn_args) == len(_BM_SPECS[name]['dyn'])
  assert len(tpl_args) == len(_BM_SPECS[name]['tpl'])
  out['name'] = name
  out['cpp_name'] = cpp_name
  out.update(dict((k, numericalize(v)) for k, v in zip(_BM_SPECS[name]['dyn'], dyn_args)))
  out.update(dict(zip(_BM_SPECS[name]['tpl'], tpl_args)))
  return out

def expand_json(js, js2 = None):
  if not js and not js2: raise StopIteration()
  if not js: js = js2
  for bm in js['benchmarks']:
    if bm['name'].endswith('_stddev') or bm['name'].endswith('_mean'): continue
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
    if js2:
      for bm2 in js2['benchmarks']:
        if bm['name'] == bm2['name'] and 'already_used' not in bm2:
          row['cpu_time'] = bm2['cpu_time']
          row['real_time'] = bm2['real_time']
          row['iterations'] = bm2['iterations']
          bm2['already_used'] = True
          break
    yield row
