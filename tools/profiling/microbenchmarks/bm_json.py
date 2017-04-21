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
    'tpl': ['fixture'],
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
  assert name in _BM_SPECS, '_BM_SPECS needs to be expanded for %s' % name
  assert len(dyn_args) == len(_BM_SPECS[name]['dyn'])
  assert len(tpl_args) == len(_BM_SPECS[name]['tpl'])
  out['name'] = name
  out['cpp_name'] = cpp_name
  out.update(dict((k, numericalize(v)) for k, v in zip(_BM_SPECS[name]['dyn'], dyn_args)))
  out.update(dict(zip(_BM_SPECS[name]['tpl'], tpl_args)))
  return out

def expand_json(js, js2 = None):
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
