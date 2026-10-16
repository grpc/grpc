# Copyright 2022 gRPC authors.
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

import subprocess
import sys
import tempfile


def run_buildozer(buildozer_commands):
    if not buildozer_commands:
        return
    ok_statuses = (0, 3)
    temp = tempfile.NamedTemporaryFile()
    open(temp.name, "w").write("\n".join(buildozer_commands))
    c = ["tools/distrib/buildozer.sh", "-f", temp.name]
    r = subprocess.call(c)
    if r not in ok_statuses:
        print("{} failed with status {}".format(c, r))
        sys.exit(1)
