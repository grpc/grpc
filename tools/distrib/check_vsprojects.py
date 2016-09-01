#!/usr/bin/env python2.7

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

