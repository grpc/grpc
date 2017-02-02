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
