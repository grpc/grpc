#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`
python2.7_virtual_environment/bin/python2.7 -B -m unittest discover -s src/python -p '*.py'
python3.4 -B -m unittest discover -s src/python -p '*.py'
