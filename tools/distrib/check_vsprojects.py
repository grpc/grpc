#!/usr/bin/env python2.7

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

import os
import re
import sys

from lxml import etree


def main():
  root_dir = os.path.abspath(
      os.path.join(os.path.dirname(sys.argv[0]), '../..'))
  os.chdir(root_dir)

  project_re = re.compile('Project\(.*\) = ".+", "(.+)", "(.+)"')

  known_projects = {}
  with open(os.path.join('vsprojects', 'grpc.sln')) as f:
    for line in f.readlines():
      m = project_re.match(line)
      if not m:
        continue

      vcxproj_path, project_guid = m.groups()
      if os.name != 'nt':
        vcxproj_path = vcxproj_path.replace('\\', '/')

      known_projects[project_guid] = vcxproj_path

  ok = True
  for vcxproj_path in known_projects.values():
    with open(os.path.join(root_dir, 'vsprojects', vcxproj_path)) as f:
      tree = etree.parse(f)

    namespaces = {'ns': 'http://schemas.microsoft.com/developer/msbuild/2003'}
    referenced_projects = tree.getroot().xpath('/ns:Project/ns:ItemGroup'
                                               '/ns:ProjectReference'
                                               '/ns:Project',
                                               namespaces=namespaces)
    for referenced_project in referenced_projects:
      # Project tag under ProjectReference is a GUID reference.
      if referenced_project.text not in known_projects:
        target_vcxproj = referenced_project.getparent().attrib['Include']
        guid = referenced_project.text
        print ('In project "%s", dependency "%s" (with GUID "%s") is not in '
               'grpc.sln' % (vcxproj_path, target_vcxproj, guid))
        ok = False

  if not ok:
    exit(1)


if __name__ == '__main__':
  main()

