#!/usr/bin/env python2.7
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

import os
import sys

os.chdir(os.path.dirname(sys.argv[0]))

streams = {
    "server_hanging_response_1_header": (
        [0, 0, 0, 4, 0, 0, 0, 0, 0]
        + [0, 0, 0, 1, 5, 0, 0, 0, 1]  # settings frame  # trailers
    ),
    "server_hanging_response_2_header2": (
        [0, 0, 0, 4, 0, 0, 0, 0, 0]
        + [0, 0, 0, 1, 4, 0, 0, 0, 1]  # settings frame
        + [0, 0, 0, 1, 5, 0, 0, 0, 1]  # headers  # trailers
    ),
}

for name, stream in streams.items():
    open("client_fuzzer_corpus/%s" % name, "w").write(bytearray(stream))
