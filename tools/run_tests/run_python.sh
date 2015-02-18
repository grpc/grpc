#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`
export LD_LIBRARY_PATH=$root/libs/opt
source python2.7_virtual_environment/bin/activate
# TODO(issue 215): Properly itemize these in run_tests.py so that they can be parallelized.
python2.7 -B -m grpc._adapter._blocking_invocation_inline_service_test
python2.7 -B -m grpc._adapter._c_test
python2.7 -B -m grpc._adapter._event_invocation_synchronous_event_service_test
python2.7 -B -m grpc._adapter._future_invocation_asynchronous_event_service_test
python2.7 -B -m grpc._adapter._links_test
python2.7 -B -m grpc._adapter._lonely_rear_link_test
python2.7 -B -m grpc._adapter._low_test
python2.7 -B -m grpc.framework.base.packets.implementations_test
python2.7 -B -m grpc.framework.face.blocking_invocation_inline_service_test
python2.7 -B -m grpc.framework.face.event_invocation_synchronous_event_service_test
python2.7 -B -m grpc.framework.face.future_invocation_asynchronous_event_service_test
python2.7 -B -m grpc.framework.foundation._later_test
python2.7 -B -m grpc.framework.foundation._logging_pool_test
# TODO(nathaniel): Get tests working under 3.4 (requires 3.X-friendly protobuf)
# python3.4 -B -m unittest discover -s src/python -p '*.py'
