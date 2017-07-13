# Copyright 2016 gRPC authors.
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
