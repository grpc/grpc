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
"""Buildgen vsprojects plugin.

This parses the list of libraries, and generates globals "vsprojects"
and "vsproject_dict", to be used by the visual studio generators.

"""

import hashlib
import re


def mako_plugin(dictionary):
    """The exported plugin code for generate_vsprojeccts

  We want to help the work of the visual studio generators.

  """

    libs = dictionary.get('libs', [])
    targets = dictionary.get('targets', [])

    for lib in libs:
        lib['is_library'] = True
    for target in targets:
        target['is_library'] = False

    projects = []
    projects.extend(libs)
    projects.extend(targets)
    for target in projects:
        if 'build' in target and target['build'] == 'test':
            default_test_dir = 'test'
        else:
            default_test_dir = '.'
        if 'vs_config_type' not in target:
            if 'build' in target and target['build'] == 'test':
                target['vs_config_type'] = 'Application'
            else:
                target['vs_config_type'] = 'StaticLibrary'
        if 'vs_packages' not in target:
            target['vs_packages'] = []
        if 'vs_props' not in target:
            target['vs_props'] = []
        target['vs_proj_dir'] = target.get('vs_proj_dir', default_test_dir)
        if target.get('vs_project_guid',
                      None) is None and 'windows' in target.get(
                          'platforms', ['windows']):
            name = target['name']
            guid = re.sub('(........)(....)(....)(....)(.*)',
                          r'{\1-\2-\3-\4-\5}',
                          hashlib.md5(name).hexdigest())
            target['vs_project_guid'] = guid.upper()
    # Exclude projects without a visual project guid, such as the tests.
    projects = [
        project for project in projects if project.get('vs_project_guid', None)
    ]

    projects = [
        project for project in projects
        if project['language'] != 'c++' or project['build'] == 'all' or
        project['build'] == 'protoc' or (project['language'] == 'c++' and (
            project['build'] == 'test' or project['build'] == 'private'))
    ]

    project_dict = dict([(p['name'], p) for p in projects])

    packages = dictionary.get('vspackages', [])
    packages_dict = dict([(p['name'], p) for p in packages])

    dictionary['vsprojects'] = projects
    dictionary['vsproject_dict'] = project_dict
    dictionary['vspackages_dict'] = packages_dict
