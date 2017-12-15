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

export GOPATH=$(pwd)/../gopath

# Use a larger heap to reduce frequency of GC mark phases, roughly without increasing their time.
# See https://software.intel.com/en-us/blogs/2014/05/10/debugging-performance-issues-in-go-programs.
# This value determined experimentally, may have a different ideal in different environments.
export GOGC=500

${GOPATH}/bin/worker $@
