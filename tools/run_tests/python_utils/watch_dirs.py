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

"""Helper to watch a (set) of directories for modifications."""

import os
import time


class DirWatcher(object):
  """Helper to watch a (set) of directories for modifications."""

  def __init__(self, paths):
    if isinstance(paths, basestring):
      paths = [paths]
    self._done = False
    self.paths = list(paths)
    self.lastrun = time.time()
    self._cache = self._calculate()

  def _calculate(self):
    """Walk over all subscribed paths, check most recent mtime."""
    most_recent_change = None
    for path in self.paths:
      if not os.path.exists(path):
        continue
      if not os.path.isdir(path):
        continue
      for root, _, files in os.walk(path):
        for f in files:
          if f and f[0] == '.': continue
          try:
            st = os.stat(os.path.join(root, f))
          except OSError as e:
            if e.errno == os.errno.ENOENT:
              continue
            raise
          if most_recent_change is None:
            most_recent_change = st.st_mtime
          else:
            most_recent_change = max(most_recent_change, st.st_mtime)
    return most_recent_change

  def most_recent_change(self):
    if time.time() - self.lastrun > 1:
      self._cache = self._calculate()
      self.lastrun = time.time()
    return self._cache

