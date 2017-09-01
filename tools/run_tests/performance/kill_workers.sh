#!/bin/bash
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

set -ex

cd $(dirname $0)/../../..

# Make sure there are no pre-existing QPS workers around before starting
# the performance test suite

# C++
killall -9 qps_worker || true

# C#
ps -C mono -o pid=,cmd= | grep QpsWorker | awk '{print $1}' | xargs kill -9 || true
ps -C dotnet -o pid=,cmd= | grep QpsWorker | awk '{print $1}' | xargs kill -9 || true

# Ruby
ps -C ruby -o pid=,cmd= | grep 'qps/worker.rb' | awk '{print $1}' | xargs kill -9 || true

# Node
ps -C node -o pid=,cmd= | grep 'performance/worker.js' | awk '{print $1}' | xargs kill -9 || true

# Python
ps -C python -o pid=,cmd= | grep 'qps_worker.py' | awk '{print $1}' | xargs kill -9 || true

# Java
jps | grep LoadWorker | awk '{print $1}' | xargs kill -9 || true

# Go
killall -9 worker || true
