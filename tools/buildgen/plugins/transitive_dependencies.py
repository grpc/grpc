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

"""Buildgen transitive dependencies

This takes the list of libs, node_modules, and targets from our
yaml dictionary, and adds to each the transitive closure
of the list of dependencies.

"""

def get_lib(libs, name):
  try:
    return next(lib for lib in libs if lib['name']==name)
  except StopIteration:
    return None

def transitive_deps(lib, libs):
  if lib is not None and 'deps' in lib:
    # Recursively call transitive_deps on each dependency, and take the union
    return set.union(set(lib['deps']),
                     *[set(transitive_deps(get_lib(libs, dep), libs))
                       for dep in lib['deps']])
  else:
    return set()

def mako_plugin(dictionary):
  """The exported plugin code for transitive_dependencies.

  Each item in libs, node_modules, and targets can have a deps list.
  We add a transitive_deps property to each with the transitive closure
  of those dependency lists.
  """
  libs = dictionary.get('libs')
  node_modules = dictionary.get('node_modules')
  targets = dictionary.get('targets')

  for target_list in (libs, targets, node_modules):
    for target in target_list:
      target['transitive_deps'] = transitive_deps(target, libs)

  python_dependencies = dictionary.get('python_dependencies')
  python_dependencies['transitive_deps'] = (
      transitive_deps(python_dependencies, libs))
