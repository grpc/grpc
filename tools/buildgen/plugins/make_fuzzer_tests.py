# Copyright 2016, Google Inc.
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

"""Create tests for each fuzzer"""

import copy
import glob

def mako_plugin(dictionary):
  targets = dictionary['targets']
  tests = dictionary['tests']
  for tgt in targets:
    if tgt['build'] == 'fuzzer':
      new_target = copy.deepcopy(tgt)
      new_target['build'] = 'test'
      new_target['name'] += '_one_entry'
      new_target['run'] = False
      new_target['src'].append('test/core/util/one_corpus_entry_fuzzer.c')
      new_target['own_src'].append('test/core/util/one_corpus_entry_fuzzer.c')
      targets.append(new_target)
      for corpus in new_target['corpus_dirs']:
        for fn in sorted(glob.glob('%s/*' % corpus)):
          tests.append({
              'name': new_target['name'],
              'args': [fn],
              'exclude_iomgrs': ['uv'],
              'exclude_configs': ['tsan'],
              'uses_polling': False,
              'platforms': ['mac', 'linux'],
              'ci_platforms': ['linux'],
              'flaky': False,
              'language': 'c',
              'cpu_cost': 0.1,
          })
