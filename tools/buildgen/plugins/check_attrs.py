# Copyright 2019 gRPC authors.
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
"""Buildgen attribute validation plugin."""


def anything():
    return lambda v: None


def one_of(values):
    return lambda v: ('{0} is not in [{1}]'.format(v, values)
                      if v not in values else None)


def subset_of(values):
    return lambda v: ('{0} is not subset of [{1}]'.format(v, values)
                      if not all(e in values for e in v) else None)


VALID_ATTRIBUTE_KEYS_MAP = {
    'filegroup': {
        'deps': anything(),
        'headers': anything(),
        'plugin': anything(),
        'public_headers': anything(),
        'src': anything(),
        'uses': anything(),
    },
    'lib': {
        'asm_src': anything(),
        'baselib': anything(),
        'boringssl': one_of((True,)),
        'build_system': anything(),
        'build': anything(),
        'cmake_target': anything(),
        'defaults': anything(),
        'deps_linkage': one_of(('static',)),
        'deps': anything(),
        'dll': one_of((True, 'only')),
        'filegroups': anything(),
        'generate_plugin_registry': anything(),
        'headers': anything(),
        'language': one_of(('c', 'c++', 'csharp')),
        'LDFLAGS': anything(),
        'platforms': subset_of(('linux', 'mac', 'posix', 'windows')),
        'public_headers': anything(),
        'secure': one_of(('check', True, False)),
        'src': anything(),
        'vs_proj_dir': anything(),
        'zlib': one_of((True,)),
    },
    'target': {
        'args': anything(),
        'benchmark': anything(),
        'boringssl': one_of((True,)),
        'build': anything(),
        'ci_platforms': anything(),
        'corpus_dirs': anything(),
        'cpu_cost': anything(),
        'defaults': anything(),
        'deps': anything(),
        'dict': anything(),
        'exclude_configs': anything(),
        'exclude_iomgrs': anything(),
        'excluded_poll_engines': anything(),
        'filegroups': anything(),
        'flaky': one_of((True, False)),
        'gtest': one_of((True, False)),
        'headers': anything(),
        'language': one_of(('c', 'c89', 'c++', 'csharp')),
        'maxlen': anything(),
        'platforms': subset_of(('linux', 'mac', 'posix', 'windows')),
        'run': one_of((True, False)),
        'secure': one_of(('check', True, False)),
        'src': anything(),
        'timeout_seconds': anything(),
        'uses_polling': anything(),
        'vs_proj_dir': anything(),
        'zlib': one_of((True,)),
    },
}


def check_attributes(entity, kind, errors):
    attributes = VALID_ATTRIBUTE_KEYS_MAP[kind]
    name = entity.get('name', anything())
    for key, value in entity.items():
        if key == 'name':
            continue
        validator = attributes.get(key)
        if validator:
            error = validator(value)
            if error:
                errors.append(
                    "{0}({1}) has an invalid value for '{2}': {3}".format(
                        name, kind, key, error))
        else:
            errors.append("{0}({1}) has an invalid attribute '{2}'".format(
                name, kind, key))


def mako_plugin(dictionary):
    """The exported plugin code for check_attr.

  This validates that filegroups, libs, and target can have only valid
  attributes. This is mainly for preventing build.yaml from having
  unnecessary and misleading attributes accidentally.
  """

    errors = []
    for filegroup in dictionary.get('filegroups', {}):
        check_attributes(filegroup, 'filegroup', errors)
    for lib in dictionary.get('libs', {}):
        check_attributes(lib, 'lib', errors)
    for target in dictionary.get('targets', {}):
        check_attributes(target, 'target', errors)
    if errors:
        raise Exception('\n'.join(errors))
