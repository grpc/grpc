#!/bin/bash
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

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`
export LD_LIBRARY_PATH=$root/libs/opt
source python2.7_virtual_environment/bin/activate
# TODO(issue 215): Properly itemize these in run_tests.py so that they can be parallelized.
# TODO(atash): Enable dynamic unused port discovery for this test.
# TODO(mlumish): Re-enable this test when we can install protoc
# python2.7 -B test/compiler/python_plugin_test.py --build_mode=opt
python2.7 -B -m grpc._adapter._blocking_invocation_inline_service_test
python2.7 -B -m grpc._adapter._c_test
python2.7 -B -m grpc._adapter._event_invocation_synchronous_event_service_test
python2.7 -B -m grpc._adapter._future_invocation_asynchronous_event_service_test
python2.7 -B -m grpc._adapter._links_test
python2.7 -B -m grpc._adapter._lonely_rear_link_test
python2.7 -B -m grpc._adapter._low_test
python2.7 -B -m grpc.early_adopter.implementations_test
python2.7 -B -m grpc.framework.assembly.implementations_test
python2.7 -B -m grpc.framework.base.packets.implementations_test
python2.7 -B -m grpc.framework.face.blocking_invocation_inline_service_test
python2.7 -B -m grpc.framework.face.event_invocation_synchronous_event_service_test
python2.7 -B -m grpc.framework.face.future_invocation_asynchronous_event_service_test
python2.7 -B -m grpc.framework.foundation._later_test
python2.7 -B -m grpc.framework.foundation._logging_pool_test
# TODO(nathaniel): Get tests working under 3.4 (requires 3.X-friendly protobuf)
# python3.4 -B -m unittest discover -s src/python -p '*.py'
