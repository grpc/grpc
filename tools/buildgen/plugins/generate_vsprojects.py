# Copyright 2015, Google Inc.
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
    if target.get('vs_project_guid', None) is None and 'windows' in target.get('platforms', ['windows']):
      name = target['name']
      guid = re.sub('(........)(....)(....)(....)(.*)',
             r'{\1-\2-\3-\4-\5}',
             hashlib.md5(name).hexdigest())
      target['vs_project_guid'] = guid.upper()
  # Exclude projects without a visual project guid, such as the tests.
  projects = [project for project in projects
                if project.get('vs_project_guid', None)]

  projects = [project for project in projects
                if project['language'] != 'c++' or project['build'] == 'all' or project['build'] == 'protoc' or (project['language'] == 'c++' and  (project['build'] == 'test' or project['build'] == 'private'))]

  project_dict = dict([(p['name'], p) for p in projects])

  packages = dictionary.get('vspackages', [])
  packages_dict = dict([(p['name'], p) for p in packages])

  dictionary['vsprojects'] = projects
  dictionary['vsproject_dict'] = project_dict
  dictionary['vspackages_dict'] = packages_dict
