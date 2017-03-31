#!/bin/bash
# Copyright 2016, Google Inc.
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

# Don't run this script standalone. Instead, run from the repository root:
# ./tools/run_tests/run_tests.py -l objc

set -evo pipefail

cd `dirname $0`

trap 'echo "EXIT TIME:  $(date)"' EXIT

echo "TIME:  $(date)"
SCHEME=HelloWorld                              \
  EXAMPLE_PATH=examples/objective-c/helloworld \
  ./build_one_example.sh

echo "TIME:  $(date)"
SCHEME=RouteGuideClient                         \
  EXAMPLE_PATH=examples/objective-c/route_guide \
  ./build_one_example.sh

echo "TIME:  $(date)"
SCHEME=AuthSample                               \
  EXAMPLE_PATH=examples/objective-c/auth_sample \
  ./build_one_example.sh

rm -f ../examples/RemoteTestClient/*.{h,m}

echo "TIME:  $(date)"
SCHEME=Sample                                  \
  EXAMPLE_PATH=src/objective-c/examples/Sample \
  ./build_one_example.sh

echo "TIME:  $(date)"
SCHEME=Sample                                  \
  EXAMPLE_PATH=src/objective-c/examples/Sample \
  FRAMEWORKS=YES                               \
  ./build_one_example.sh

echo "TIME:  $(date)"
SCHEME=SwiftSample                                  \
  EXAMPLE_PATH=src/objective-c/examples/SwiftSample \
  ./build_one_example.sh

