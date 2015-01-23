#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`
virtualenv python2.7_virtual_environment
python2.7_virtual_environment/bin/pip install enum34==1.0.4 futures==2.2.0
