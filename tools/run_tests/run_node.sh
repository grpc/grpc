#!/bin/bash

set -ex

# change to grpc repo root
cd $(dirname $0)/../..

root=`pwd`

$root/src/node/node_modules/mocha/bin/mocha $root/src/node/test
