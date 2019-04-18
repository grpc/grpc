#!/bin/sh
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


set -e

if [ "x$TEST" = "x" ] ; then
  TEST=false
fi


cd `dirname $0`/../..
mako_renderer=tools/buildgen/mako_renderer.py

if [ "x$TEST" != "x" ] ; then
  tools/buildgen/build-cleaner.py build.yaml
fi

. tools/buildgen/generate_build_additions.sh

tools/buildgen/generate_projects.py build.yaml $gen_build_files $*

rm $gen_build_files
