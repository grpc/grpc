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
                        most_recent_change = max(most_recent_change,
                                                 st.st_mtime)
        return most_recent_change

    def most_recent_change(self):
        if time.time() - self.lastrun > 1:
            self._cache = self._calculate()
            self.lastrun = time.time()
        return self._cache
