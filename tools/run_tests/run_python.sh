#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`
PYTHONPATH=third_party/protobuf/python python2.7_virtual_environment/bin/python2.7 -B -m unittest discover -s src/python -p '*.py'
# TODO(nathaniel): Get this working again (requires 3.X-friendly protobuf)
# python3.4 -B -m unittest discover -s src/python -p '*.py'
