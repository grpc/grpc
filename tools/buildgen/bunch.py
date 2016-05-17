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

"""Allows dot-accessible dictionaries."""


class Bunch(dict):

  def __init__(self, d):
    dict.__init__(self, d)
    self.__dict__.update(d)


# Converts any kind of variable to a Bunch
def to_bunch(var):
  if isinstance(var, list):
    return [to_bunch(i) for i in var]
  if isinstance(var, dict):
    ret = {}
    for k, v in var.items():
      if isinstance(v, (list, dict)):
        v = to_bunch(v)
      ret[k] = v
    return Bunch(ret)
  else:
    return var


# Merges JSON 'add' into JSON 'dst'
def merge_json(dst, add):
  if isinstance(dst, dict) and isinstance(add, dict):
    for k, v in add.items():
      if k in dst:
        if k == '#': continue
        merge_json(dst[k], v)
      else:
        dst[k] = v
  elif isinstance(dst, list) and isinstance(add, list):
    dst.extend(add)
  else:
    raise Exception('Tried to merge incompatible objects %s %s\n\n%r\n\n%r' % (type(dst).__name__, type(add).__name__, dst, add))

