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

"""Buildgen expand filegroups plugin.

This takes the list of libs from our yaml dictionary,
and expands any and all filegroup.

"""


def excluded(filename, exclude_res):
  for r in exclude_res:
    if r.search(filename):
      return True
  return False


def mako_plugin(dictionary):
  """The exported plugin code for expand_filegroups.

  The list of libs in the build.yaml file can contain "filegroups" tags.
  These refer to the filegroups in the root object. We will expand and
  merge filegroups on the src, headers and public_headers properties.

  """
  libs = dictionary.get('libs')
  filegroups_list = dictionary.get('filegroups')
  filegroups = {}

  for fg in filegroups_list:
    filegroups[fg['name']] = fg

  for lib in libs:
    for fg_name in lib.get('filegroups', []):
      fg = filegroups[fg_name]

      src = lib.get('src', [])
      src.extend(fg.get('src', []))
      lib['src'] = src

      headers = lib.get('headers', [])
      headers.extend(fg.get('headers', []))
      lib['headers'] = headers

      public_headers = lib.get('public_headers', [])
      public_headers.extend(fg.get('public_headers', []))
      lib['public_headers'] = public_headers
