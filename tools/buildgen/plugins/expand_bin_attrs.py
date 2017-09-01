# Copyright 2015 gRPC authors.
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

"""Buildgen expand binary attributes plugin.

This fills in any optional attributes.

"""


def mako_plugin(dictionary):
  """The exported plugin code for expand_filegroups.

  The list of libs in the build.yaml file can contain "filegroups" tags.
  These refer to the filegroups in the root object. We will expand and
  merge filegroups on the src, headers and public_headers properties.

  """

  targets = dictionary.get('targets')
  default_platforms = ['windows', 'posix', 'linux', 'mac']

  for tgt in targets:
    tgt['flaky'] = tgt.get('flaky', False)
    tgt['platforms'] = sorted(tgt.get('platforms', default_platforms))
    tgt['ci_platforms'] = sorted(tgt.get('ci_platforms', tgt['platforms']))
    tgt['boringssl'] = tgt.get('boringssl', False)
    tgt['zlib'] = tgt.get('zlib', False)
    tgt['ares'] = tgt.get('ares', False)
    tgt['gtest'] = tgt.get('gtest', False)

  libs = dictionary.get('libs')
  for lib in libs:
    lib['boringssl'] = lib.get('boringssl', False)
    lib['zlib'] = lib.get('zlib', False)
    lib['ares'] = lib.get('ares', False)
