#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`
# TODO(issue 215): Properly itemize these in run_tests.py so that they can be parallelized.
python2.7_virtual_environment/bin/python2.7 -B -m _adapter._blocking_invocation_inline_service_test
python2.7_virtual_environment/bin/python2.7 -B -m _adapter._c_test
python2.7_virtual_environment/bin/python2.7 -B -m _adapter._event_invocation_synchronous_event_service_test
python2.7_virtual_environment/bin/python2.7 -B -m _adapter._future_invocation_asynchronous_event_service_test
python2.7_virtual_environment/bin/python2.7 -B -m _adapter._links_test
python2.7_virtual_environment/bin/python2.7 -B -m _adapter._lonely_rear_link_test
python2.7_virtual_environment/bin/python2.7 -B -m _adapter._low_test
python2.7_virtual_environment/bin/python2.7 -B -m _framework.base.packets.implementations_test
python2.7_virtual_environment/bin/python2.7 -B -m _framework.face.blocking_invocation_inline_service_test
python2.7_virtual_environment/bin/python2.7 -B -m _framework.face.event_invocation_synchronous_event_service_test
python2.7_virtual_environment/bin/python2.7 -B -m _framework.face.future_invocation_asynchronous_event_service_test
python2.7_virtual_environment/bin/python2.7 -B -m _framework.foundation._later_test
python2.7_virtual_environment/bin/python2.7 -B -m _framework.foundation._logging_pool_test
# TODO(nathaniel): Get tests working under 3.4 (requires 3.X-friendly protobuf)
# python3.4 -B -m unittest discover -s src/python -p '*.py'
