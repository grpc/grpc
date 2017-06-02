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

"""Buildgen resolved dependencies

This takes the list of libs, node_modules, and targets from our
yaml dictionary, and adds to each a corresponding list of dependency
object references.

"""

def get_lib(libs, name):
  return next(lib for lib in libs if lib['name']==name)

def resolved_deps(target, libs):
  if 'deps' in target:
    return [get_lib(libs, dep) for dep in target['deps']]
  else:
    return []

def mako_plugin(dictionary):
  """The exported plugin code for resolved_dependencies.

  Each item in libs, node_modules, and targets can have a deps list.
  We add a resolved_deps property to each with the corresponding list
  of lib objects
  """
  libs = dictionary.get('libs')
  node_modules = dictionary.get('node_modules')
  targets = dictionary.get('targets')

  for target_list in (libs, node_modules, targets):
    for target in target_list:
      target['resolved_deps'] = resolved_deps(target, libs)
