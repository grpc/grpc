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

"""Buildgen .proto files list plugin.

This parses the list of targets from the yaml build file, and creates
a list called "protos" that contains all of the proto file names.

"""


import re


def mako_plugin(dictionary):
  """The exported plugin code for list_protos.

  Some projects generators may want to get the full list of unique .proto files
  that are being included in a project. This code extracts all files referenced
  in any library or target that ends in .proto, and builds and exports that as
  a list called "protos".

  """

  libs = dictionary.get('libs', [])
  targets = dictionary.get('targets', [])

  proto_re = re.compile('(.*)\\.proto')

  protos = set()
  for lib in libs:
    for src in lib.get('src', []):
      m = proto_re.match(src)
      if m:
        protos.add(m.group(1))
  for tgt in targets:
    for src in tgt.get('src', []):
      m = proto_re.match(src)
      if m:
        protos.add(m.group(1))

  protos = sorted(protos)

  dictionary['protos'] = protos
