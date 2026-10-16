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
"""Constants shared among tests throughout RPC Framework."""

# Value for maximum duration in seconds that a test is allowed for its actual
# behavioral logic, excluding all time spent deliberately waiting in the test.
TIME_ALLOWANCE = 10
# Value for maximum duration in seconds of RPCs that may time out as part of a
# test.
SHORT_TIMEOUT = 4
# Absurdly large value for maximum duration in seconds for should-not-time-out
# RPCs made during tests.
LONG_TIMEOUT = 3000
# Values to supply on construction of an object that will service RPCs; these
# should not be used as the actual timeout values of any RPCs made during tests.
DEFAULT_TIMEOUT = 300
MAXIMUM_TIMEOUT = 3600

# The number of payloads to transmit in streaming tests.
STREAM_LENGTH = 200

# The size of payloads to transmit in tests.
PAYLOAD_SIZE = 256 * 1024 + 17

# The concurrency to use in tests of concurrent RPCs that will not create as
# many threads as RPCs.
RPC_CONCURRENCY = 200

# The concurrency to use in tests of concurrent RPCs that will create as many
# threads as RPCs.
THREAD_CONCURRENCY = 25

# The size of thread pools to use in tests.
POOL_SIZE = 10
